// clawd-mochi-m5dial — 入口点
// M5Dial.begin() 内部处理 G46 HOLD pin、Display、Touch、Power、I2C。详见 CLAUDE.md §9。
// 网络模型（v0.2.0）：默认开机进设备界面（动画）；有凭据则后台连 STA 起 web 控制，无网则单机运行。
// 配网二维码屏为按需进入（BtnA 长按 / Settings Reset WiFi 置标志 + 重启）。详见 §9 / provisioning。

#include <M5Dial.h>

#include "mode_manager.h"
#include "state.h"
#include "config.h"
#include "modes/eyes_normal.h"
#include "modes/eyes_squish.h"
#include "modes/claude_code.h"
#include "modes/canvas.h"
#include "modes/claude_status.h"
#include "modes/face_show.h"
#include "modes/pc_monitor.h"
#include "modes/reminder_overlay.h"
#include "services/provisioning.h"
#include "services/reminder.h"
#include "web/ap_server.h"

#include <cstring>

static mochi::EyesNormal   g_eyes_normal;
static mochi::EyesSquish   g_eyes_squish;
static mochi::ClaudeCode   g_claude_code;
static mochi::Canvas       g_canvas;
static mochi::ClaudeStatus g_claude_status;
static mochi::FaceShow     g_face_show;
static mochi::PcMonitor    g_pc_monitor;
static mochi::ReminderOverlay g_reminder_overlay;

void setup() {
    auto cfg = M5.config();
    M5Dial.begin(cfg, /*enableEncoder=*/true, /*enableRFID=*/false);
    M5Dial.BtnA.setHoldThresh(5000);  // BtnA 长按 5s → 复位配网（Phase 10）
    M5Dial.Display.setBrightness(
        mochi::state::g_state.backlight ? mochi::state::kBacklightOnLevel
                                        : mochi::state::kBacklightOffLevel);

    mochi::g_mochi.registerMode(&g_eyes_normal);
    mochi::g_mochi.registerMode(&g_eyes_squish);
    mochi::g_mochi.registerMode(&g_claude_code);
    mochi::g_mochi.registerMode(&g_canvas);
    mochi::g_mochi.registerMode(&g_claude_status);
    mochi::g_mochi.registerMode(&g_face_show);
    mochi::g_mochi.registerMode(&g_pc_monitor);
    mochi::g_mochi.registerMode(&g_reminder_overlay);

    // 启动决策（v0.2.0）：默认进设备界面（动画），配网为按需（详见 CLAUDE.md §9）。
    //   force_setup（BtnA 长按 / Settings Reset WiFi 置位）→ 配网二维码屏；
    //   否则进设备界面——有凭据则后台连 STA 起 web 控制，连不上也只是无网单机运行。
    const bool force_setup = mochi::provisioning::consumeSetupRequest();
    const bool has_creds   = mochi::provisioning::begin();
    mochi::reminder::begin();  // Phase 13：载入 NVS 提醒表

    if (force_setup) {
        mochi::provisioning::startSetupAP();
        mochi::web::WebStack::begin(/*setup_mode=*/true);
        mochi::provisioning::drawSetupScreen();  // 配网模式不 setMode（避免盖掉二维码）
        Serial.println("clawd-mochi-m5dial v0.2.0 boot (setup AP, on-demand)");
    } else {
        bool connected = false;
        if (has_creds) {
            connected = mochi::provisioning::connectSTA(mochi::config::kStaConnectTimeoutMs);
            if (!connected) {                 // 重试一次（mumuer1024 同款）
                delay(3000);
                connected = mochi::provisioning::connectSTA(mochi::config::kStaConnectTimeoutMs);
            }
        }
        if (connected) {
            mochi::web::WebStack::begin(/*setup_mode=*/false);
            mochi::provisioning::startMdns();
            configTime(mochi::config::kTzOffsetSec, 0,
                       mochi::config::kNtpServer1, mochi::config::kNtpServer2);  // Phase 13：NTP 取墙钟（提醒触发依据）
            Serial.println("clawd-mochi-m5dial v0.2.0 boot (STA + device UI)");
        } else {
            // 无凭据或连不上：单机运行，不自动进配网（按需经长按进入）
            Serial.println("clawd-mochi-m5dial v0.2.0 boot (standalone device UI)");
        }
        mochi::g_mochi.setMode(mochi::state::g_state.current_mode);  // 默认 NORMAL_EYES
    }
}

void loop() {
    M5Dial.update();
    const uint32_t now = millis();

    if (mochi::provisioning::isSetupMode()) {
        mochi::provisioning::tickDNS();
    } else {
        if (M5Dial.BtnA.wasHold()) {  // 长按 5s：进配网二维码屏（按需连接，保留已存凭据）
            mochi::provisioning::requestSetupMode();
            mochi::provisioning::requestRestart(300);
        }
        // Phase 11：消费 /cc 暂存的 Claude Code 状态事件（在 loop 上下文切模式 + 绘屏，§1.6）
        // Phase 11b：按项目作用域过滤——cc_scope 空 = 全局接受；非空仅精确匹配的项目驱动设备。
        mochi::state::CcStatus cc;
        char cc_project[mochi::config::kCcTokenLen] = {0};
        if (mochi::web::WebStack::consumeCcEvent(cc, cc_project, sizeof(cc_project))) {
            mochi::provisioning::registerCcProject(cc_project);
            const char* scope = mochi::state::g_state.cc_scope;
            const bool accept = (scope[0] == '\0') || (strcmp(scope, cc_project) == 0);
            if (accept) {
                mochi::state::g_state.cc_status = cc;
                const auto cur = mochi::g_mochi.currentId();
                if (cur != mochi::ModeId::CANVAS && cur != mochi::ModeId::REMINDER_OVERLAY) {  // canvas / 闹钟 overlay 不打断
                    if (cur != mochi::ModeId::CLAUDE_STATUS) {
                        mochi::state::g_state.current_mode = mochi::ModeId::CLAUDE_STATUS;
                        mochi::g_mochi.setMode(mochi::ModeId::CLAUDE_STATUS);  // 自动切入联动
                    } else if (auto* m = mochi::g_mochi.currentMode()) {
                        m->applyState(mochi::state::g_state);  // 已在联动：刷新状态表情
                    }
                }
            }
            mochi::web::WebStack::broadcastState();  // 同步 mode / 徽标 / cc_projects 下拉
        }
        // Phase 13：提醒到点 → 切瞬态 overlay（不改 g_state.current_mode；30s 或轻点 BtnA 后回原 mode）
        mochi::reminder::tick(now);
        char remind_msg[mochi::config::kReminderMsgLen];
        if (mochi::reminder::consumeFired(remind_msg, sizeof(remind_msg))) {
            mochi::ModeId ret = mochi::g_mochi.currentId();
            if (ret == mochi::ModeId::REMINDER_OVERLAY) ret = mochi::state::g_state.current_mode;
            g_reminder_overlay.show(remind_msg, ret, now);
            mochi::g_mochi.setMode(mochi::ModeId::REMINDER_OVERLAY);
        }
        if (mochi::g_mochi.currentId() == mochi::ModeId::REMINDER_OVERLAY &&
            (g_reminder_overlay.finished(now) || M5Dial.BtnA.wasClicked())) {
            mochi::g_mochi.setMode(g_reminder_overlay.returnMode());  // 回到底层 mode（其 onEnter 重绘）
        }
        mochi::g_mochi.tick(now);
        mochi::web::WebStack::tick(now);
    }

    mochi::provisioning::tickRestart(now);
    delay(10);
}
