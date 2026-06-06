// clawd-mochi-m5dial — 入口点
// M5Dial.begin() 内部处理 G46 HOLD pin、Display、Touch、Encoder、Speaker、Power、I2C。详见 CLAUDE.md §9。
// 网络模型（v0.2.0）：默认开机进设备界面（动画）；有凭据则后台连 STA 起 web 控制，无网则单机运行。
// 配网二维码屏为按需进入（BtnA 长按 / Settings Reset WiFi 置标志 + 重启）。详见 §9 / provisioning。
// v0.3.0：本地硬件交互——旋钮单击轮询 mode / 旋转 = 上下文拨盘 / 触摸 tap·长按·拖动 / 蜂鸣器 earcon。
//          原 Normal/Squish Eyes 已并入统一 FACE_SHOW（含 anim_idle 摇摆眼 home）。

#include <M5Dial.h>

#include "mode_manager.h"
#include "state.h"
#include "config.h"
#include "modes/claude_code.h"
#include "modes/canvas.h"
#include "modes/claude_status.h"
#include "modes/face_show.h"
#include "modes/pc_monitor.h"
#include "modes/reminder_overlay.h"
#include "faces/faces_data.h"
#include "input/encoder.h"
#include "input/touch.h"
#include "audio/beeper.h"
#include "ui/transition.h"
#include "services/provisioning.h"
#include "services/reminder.h"
#include "web/ap_server.h"

#include <esp_random.h>
#include <cstring>

// 命名空间别名 / 引入，缩短 dispatch 代码（仅本 TU）。
using mochi::ModeId;
using mochi::g_mochi;
namespace state        = mochi::state;
namespace config       = mochi::config;
namespace faces        = mochi::faces;
namespace web          = mochi::web;
namespace provisioning = mochi::provisioning;
namespace beeper       = mochi::audio::beeper;
namespace encoder      = mochi::input::encoder;
namespace touch        = mochi::input::touch;
namespace ui           = mochi::ui;

// ── mode 实例（生命周期由本 TU 持有，注册进 g_mochi）──
static mochi::ClaudeCode     g_claude_code;
static mochi::Canvas         g_canvas;
static mochi::ClaudeStatus   g_claude_status;
static mochi::FaceShow       g_face_show;
static mochi::PcMonitor      g_pc_monitor;
static mochi::ReminderOverlay g_reminder_overlay;

// ── v0.3.0 dispatch 运行时态 ──
static uint32_t g_last_input_ms = 0;     // 任意本地输入时刻（idle doze 依据）
static uint32_t g_last_cycle_ms = 0;     // Faces 自动轮换计时锚
static bool     g_dozing        = false; // 打盹中（FACE_SHOW 静置超时）
static bool     g_drag_active   = false; // Canvas 触摸拖动中（落笔音去重）

// 打盹中收到任意输入 → 唤醒（恢复亮度 + 哈欠 + 回 idle 脸）。返回 true 表示本次输入已被吞作唤醒。
static bool wakeFromDoze(uint32_t now) {
    if (!g_dozing) return false;
    g_dozing = false;
    M5Dial.Display.setBrightness(state::g_state.backlight ? state::kBacklightOnLevel
                                                          : state::kBacklightOffLevel);
    beeper::yawn();
    state::g_state.face_index = faces::kFaceIdle;
    g_last_cycle_ms = now;
    if (auto* m = g_mochi.currentMode()) m->applyState(state::g_state);
    web::WebStack::broadcastState();
    return true;
}

// 旋钮单击：星芒转场 + 轮询下一 mode。
static void handleButton(uint32_t now) {
    if (!M5Dial.BtnA.wasClicked()) return;
    g_last_input_ms = now;
    if (wakeFromDoze(now)) return;                 // 吞掉唤醒点击
    const ModeId next = g_mochi.nextInCycle();
    beeper::modeSwitch();
    ui::playSparkleWipe(state::g_state.bg_color_565);
    state::g_state.current_mode = next;
    g_mochi.setMode(next);                         // onEnter 在干净底色上重绘
    web::WebStack::broadcastState();
}

// 编码器在 Claude Link：在 [全部] + 已注册项目间步进 cc_scope（选要反映哪个会话）。
static void stepCcScope(int d) {
    const uint8_t pc = provisioning::ccProjectCount();
    const int total = pc + 1;                      // idx 0 = 全部（空 scope）
    int cur = 0;
    const char* scope = state::g_state.cc_scope;
    if (scope[0] != '\0') {
        for (uint8_t i = 0; i < pc; ++i)
            if (strcmp(scope, provisioning::ccProjectAt(i)) == 0) { cur = i + 1; break; }
    }
    int ni = (cur + d) % total;
    if (ni < 0) ni += total;
    provisioning::saveCcScope(ni == 0 ? "" : provisioning::ccProjectAt(ni - 1));
    web::WebStack::broadcastState();
}

// 旋转 = 上下文拨盘（作用于当前界面内容）。
static void handleEncoder(uint32_t now) {
    const int d = encoder::poll();
    if (d == 0) return;
    g_last_input_ms = now;
    beeper::tick();
    if (wakeFromDoze(now)) return;
    switch (g_mochi.currentId()) {
        case ModeId::FACE_SHOW: {
            const int n = faces::kFaceCount;
            int idx = (static_cast<int>(state::g_state.face_index) + d) % n;
            if (idx < 0) idx += n;
            state::g_state.face_index = static_cast<uint8_t>(idx);
            g_last_cycle_ms = now;                 // 手动浏览重置自动轮换
            if (auto* m = g_mochi.currentMode()) m->applyState(state::g_state);
            web::WebStack::broadcastState();
            break;
        }
        case ModeId::PC_MONITOR:    g_pc_monitor.nudgeCard(d); break;
        case ModeId::CLAUDE_STATUS: stepCcScope(d);            break;
        case ModeId::CANVAS:        if (g_canvas.nudgeClear(d, now)) beeper::success(); break;  // 转半圈清屏
        default: break;  // CLAUDE_CODE：旋转 no-op
    }
}

// 触摸：Canvas 原始按压态自由绘画 / 其余 mode tap 反应·长按锁定。
static void handleTouch(uint32_t now) {
    const auto ev = touch::poll();
    using G = touch::Gesture;
    // Canvas：按压即画（isDragging 8px 阈值 + 慢按误判 hold，对描画太粗，故走 pressed）。
    if (g_mochi.currentId() == ModeId::CANVAS) {
        if (ev.pressed) {
            g_last_input_ms = now;
            const uint16_t pen = state::g_state.pen_color_565;
            if (!g_drag_active) { beeper::penDown(); g_drag_active = true; g_canvas.drawDot(ev.x, ev.y, pen); }
            else g_canvas.drawStroke(ev.prev_x, ev.prev_y, ev.x, ev.y, pen);
            g_canvas.flush();
        } else {
            g_drag_active = false;
        }
        return;  // Canvas 内触摸专用于绘画，不走 tap / 长按
    }
    if (ev.gesture == G::None) return;
    g_last_input_ms = now;
    if (ev.gesture == G::Tap && M5Dial.BtnA.wasClicked()) return;  // 旋钮单击优先，防误触
    if (wakeFromDoze(now)) return;
    switch (ev.gesture) {
        case G::Tap:
            if (g_mochi.currentId() == ModeId::FACE_SHOW) g_face_show.react(now);
            break;
        case G::LongPress:  // 全局锁定：冻结 Faces 自动轮换 & PC Monitor 自动切卡
            state::g_state.auto_locked = !state::g_state.auto_locked;
            beeper::pinToggle(state::g_state.auto_locked);
            break;
        default: break;
    }
}

// Faces 自动行为：动画播一次后静置 kFaceRestMs 再随机换（完全随机，可重复并重播）。
// 锁定 / doze / 非 Faces 不换；进 Faces 重置计时。
static void handleFaceAutoCycle(uint32_t now) {
    static ModeId prev = ModeId::FACE_SHOW;
    const ModeId cur = g_mochi.currentId();
    if (cur != ModeId::FACE_SHOW) { prev = cur; return; }
    if (prev != ModeId::FACE_SHOW) { prev = cur; g_last_cycle_ms = now; return; }  // 刚进 Faces
    prev = cur;
    if (state::g_state.auto_locked || g_dozing) return;
    // 停留 = 当前脸动画时长 + 静置：动画"演示完"后再等 kFaceRestMs 才换。
    const auto& sp = faces::spec(state::g_state.face_index);
    const uint32_t play = (sp.animated && sp.frame_ms)
                          ? static_cast<uint32_t>(sp.frame_count) * sp.frame_ms : 0;
    if (now - g_last_cycle_ms < play + config::kFaceRestMs) return;
    g_last_cycle_ms = now;
    state::g_state.face_index = static_cast<uint8_t>(esp_random() % faces::kFaceCount);  // 可重复
    g_face_show.restart(now);   // 强制从头播（抽中同一张也重播）
    web::WebStack::broadcastState();
}

// Faces 无用户输入超 kIdleDozeMs → 打盹（zzz 脸 + 调暗）。唤醒由 wakeFromDoze 处理。
static void handleIdleDoze(uint32_t now) {
    if (g_dozing) return;
    if (g_mochi.currentId() != ModeId::FACE_SHOW) { g_last_input_ms = now; return; }  // 仅 Faces 打盹
    if (now - g_last_input_ms < config::kIdleDozeMs) return;
    g_dozing = true;
    state::g_state.face_index = faces::findByKey("anim_zzz");
    M5Dial.Display.setBrightness(config::kDozeBrightness);
    if (auto* m = g_mochi.currentMode()) m->applyState(state::g_state);
    web::WebStack::broadcastState();
}

void setup() {
    auto cfg = M5.config();
    M5Dial.begin(cfg, /*enableEncoder=*/true, /*enableRFID=*/false);
    M5Dial.BtnA.setHoldThresh(5000);  // BtnA 长按 5s → 复位配网（Phase 10）
    M5Dial.Display.setBrightness(
        state::g_state.backlight ? state::kBacklightOnLevel : state::kBacklightOffLevel);
    encoder::begin();  // 记录编码器基线
    beeper::begin();   // 设蜂鸣器音量（Speaker 已由 M5Dial.begin 初始化）

    g_mochi.registerMode(&g_claude_code);
    g_mochi.registerMode(&g_canvas);
    g_mochi.registerMode(&g_claude_status);
    g_mochi.registerMode(&g_face_show);
    g_mochi.registerMode(&g_pc_monitor);
    g_mochi.registerMode(&g_reminder_overlay);

    // 启动决策（v0.2.0）：默认进设备界面（动画），配网为按需（详见 CLAUDE.md §9）。
    const bool force_setup = provisioning::consumeSetupRequest();
    const bool has_creds   = provisioning::begin();
    mochi::reminder::begin();  // Phase 13：载入 NVS 提醒表

    if (force_setup) {
        provisioning::startSetupAP();
        web::WebStack::begin(/*setup_mode=*/true);
        provisioning::drawSetupScreen();  // 配网模式不 setMode（避免盖掉二维码）
        Serial.println("clawd-mochi-m5dial v0.3.0 boot (setup AP, on-demand)");
    } else {
        bool connected = false;
        if (has_creds) {
            connected = provisioning::connectSTA(config::kStaConnectTimeoutMs);
            if (!connected) {                 // 重试一次（mumuer1024 同款）
                delay(3000);
                connected = provisioning::connectSTA(config::kStaConnectTimeoutMs);
            }
        }
        if (connected) {
            web::WebStack::begin(/*setup_mode=*/false);
            provisioning::startMdns();
            configTime(config::kTzOffsetSec, 0, config::kNtpServer1, config::kNtpServer2);
            Serial.println("clawd-mochi-m5dial v0.3.0 boot (STA + device UI)");
        } else {
            Serial.println("clawd-mochi-m5dial v0.3.0 boot (standalone device UI)");
        }
        const uint32_t now = millis();
        g_last_input_ms = now;
        g_last_cycle_ms = now;
        state::g_state.face_index = faces::kFaceIdle;           // home = 摇摆眼
        g_mochi.setMode(state::g_state.current_mode);           // 默认 FACE_SHOW
        beeper::bootChime();                                    // 开机唤醒鸣音
    }
}

void loop() {
    M5Dial.update();
    const uint32_t now = millis();

    if (provisioning::isSetupMode()) {
        provisioning::tickDNS();
    } else {
        if (M5Dial.BtnA.wasHold()) {  // 长按 5s：进配网二维码屏（保留已存凭据）
            provisioning::requestSetupMode();
            provisioning::requestRestart(300);
        }
        // Phase 11：消费 /cc 暂存的 Claude Code 状态事件（loop 上下文切模式 + 绘屏，§1.6）
        state::CcStatus cc;
        char cc_project[config::kCcTokenLen] = {0};
        if (web::WebStack::consumeCcEvent(cc, cc_project, sizeof(cc_project))) {
            provisioning::registerCcProject(cc_project);
            const char* scope = state::g_state.cc_scope;
            const bool accept = (scope[0] == '\0') || (strcmp(scope, cc_project) == 0);
            if (accept) {
                // v0.3.0：Claude Link 声音通知（状态翻转时；环境通知器，不限当前 mode）
                const state::CcStatus prev = state::g_state.cc_status;
                if (cc != prev) {
                    if (cc == state::CcStatus::WAITING) beeper::attention();
                    else if (cc == state::CcStatus::IDLE &&
                             (prev == state::CcStatus::WAITING || prev == state::CcStatus::WORKING))
                        beeper::success();
                }
                state::g_state.cc_status = cc;
                const auto cur = g_mochi.currentId();
                if (cur != ModeId::CANVAS && cur != ModeId::REMINDER_OVERLAY) {  // canvas / 闹钟不打断
                    if (cur != ModeId::CLAUDE_STATUS) {
                        state::g_state.current_mode = ModeId::CLAUDE_STATUS;
                        g_mochi.setMode(ModeId::CLAUDE_STATUS);  // 自动切入联动
                    } else if (auto* m = g_mochi.currentMode()) {
                        m->applyState(state::g_state);  // 已在联动：刷新状态表情
                    }
                }
            }
            web::WebStack::broadcastState();
        }
        // Phase 13：提醒到点 → 切瞬态 overlay（不改 g_state.current_mode；30s 或轻点 BtnA 回原 mode）
        mochi::reminder::tick(now);
        char remind_msg[config::kReminderMsgLen];
        if (mochi::reminder::consumeFired(remind_msg, sizeof(remind_msg))) {
            ModeId ret = g_mochi.currentId();
            if (ret == ModeId::REMINDER_OVERLAY) ret = state::g_state.current_mode;
            g_reminder_overlay.show(remind_msg, ret, now);
            g_mochi.setMode(ModeId::REMINDER_OVERLAY);
        }

        // 输入派发 vs 提醒 overlay 互斥（同帧不双触发 BtnA）
        if (g_mochi.currentId() == ModeId::REMINDER_OVERLAY) {
            if (g_reminder_overlay.finished(now) || M5Dial.BtnA.wasClicked()) {
                g_mochi.setMode(g_reminder_overlay.returnMode());  // 回底层 mode（onEnter 重绘）
            }
        } else {
            handleButton(now);        // 旋钮单击 → 轮询 mode
            handleEncoder(now);       // 旋转 → 上下文拨盘
            handleTouch(now);         // tap / 长按 / 拖动
            handleFaceAutoCycle(now); // Faces 30s 随机
            handleIdleDoze(now);      // 闲置打盹
        }

        g_mochi.tick(now);
        web::WebStack::tick(now);
    }

    provisioning::tickRestart(now);
    delay(10);
}
