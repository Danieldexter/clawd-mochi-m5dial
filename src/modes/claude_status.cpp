#include "claude_status.h"

#include "canvas.h"          // releaseSprite()：onEnter 释放 Canvas 115KB（大缓冲互斥，§9）

#include <M5Dial.h>
#include <cmath>
#include <esp_random.h>

namespace mochi {

namespace {

// ── 几何 / 数学（spec §7）──
constexpr int16_t kCx = 120, kCy = 120;
constexpr float   kDeg2Rad = 0.01745329f;
constexpr float   kHalfPi  = 1.5707963f;

// ── 调色板索引（4bpp，0..8）──
// 绘制时传入的是"索引"（palette4 convert 会把颜色值掩成 0..15 的索引）；
// 运行时经 applyPalette() 把每个索引映射到 spec §1 的 RGB565（kBg=bg_color_）。
constexpr uint8_t kBg       = 0;  // 背景（= 运行时 bg_color_）
constexpr uint8_t kEyeColor = 1;  // 眼 / "!" 杆点（黑 0x0000）
constexpr uint8_t kGreen    = 2;  // working 绿 0x07E0
constexpr uint8_t kGreenHi  = 3;  // working 星芒高光 0x9FF3
constexpr uint8_t kAmber    = 4;  // waiting 琥珀 0xFC60
constexpr uint8_t kAmberHi  = 5;  // waiting 高光 / 环亮冠 0xFFE0
constexpr uint8_t kCream    = 6;  // 打盹 z / SPARKLE idle 静星 0xFF38
constexpr uint8_t kGlint    = 7;  // waiting 眼内白高光 0xFFFF
constexpr uint8_t kZFade    = 8;  // z 第二段淡出 0xFEF0

// ── CLAWD 眼几何 ──
constexpr int16_t kEyeW=30, kEyeH=60, kLeftX=42, kRightX=168, kEyeYT=70;
constexpr int16_t kWideW=34, kWideH=64, kWideLeftX=38, kWideRightX=168, kWideYT=64;
// idle 眼擦除盒（覆呼吸±3 / 瞟±2 / 打盹下沉+2）
constexpr int16_t kIdleClrLX=40, kIdleClrRX=166, kIdleClrY=66, kIdleClrW=34, kIdleClrH=68;
// working 下垂眼擦除盒（上沿对齐眼顶 70，避开顶星芒擦盘底 y69，risk #4）
constexpr int16_t kWorkClrLX=40, kWorkClrRX=166, kWorkClrY=70, kWorkClrW=34, kWorkClrH=62;
// waiting 大眼擦除盒
constexpr int16_t kWaitClrLX=36, kWaitClrRX=166, kWaitClrY=61, kWaitClrW=38, kWaitClrH=72;

// ── CLAWD 时序（ms / 速率）──
constexpr int16_t  kBreathAmp  = 3;
constexpr float    kBreathRate = 0.0011f;       // 周期 ~5.7s
constexpr uint32_t kBlinkGapMin=2800, kBlinkGapMax=5200;   // 随机眨眼间隔（替代节拍器）
constexpr uint32_t kBlinkDurMin=160,  kBlinkDurMax=440;    // 含偶发"困眨"长帧
constexpr uint32_t kGlanceGapMin=4000, kGlanceGapMax=9000, kGlanceHold=700;
constexpr int16_t  kGlanceDx   = 2;
constexpr uint32_t kDozeAfterMs= 12000;
constexpr int16_t  kDozeLidTop = 33, kDozeSink = 2;
constexpr uint32_t kZSpawnMs   = 2600, kZLifeMs = 2000;
constexpr int16_t  kZStartX=138, kZStartY=56, kZEndX=150, kZEndY=32, kZSizeMin=8, kZSizeMax=12;

// ── CLAWD working 顶绿星芒（中心 120,44；Lmax 锁 20 → 擦盘底 y69<眼顶70，risk #4）──
constexpr int16_t  kWSpkCx=120, kWSpkCy=44, kWSpkCore=6, kWSpkHalfW=3;
constexpr int16_t  kWSpkLmid=18, kWSpkLamp=2;   // 16..20
constexpr float    kWSpkPulse=0.00449f;         // ~1.4s
constexpr float    kWSpkRotRate=0.0364f;        // deg/ms（~36°/s，时间驱动 risk #8）
constexpr int16_t  kWorkLidTop=8, kWorkLidBot=22;          // 下垂专注
constexpr uint32_t kFocusBlinkGapMin=5000, kFocusBlinkGapMax=8000;

// ── waiting "!"（共享 CLAWD/SPARKLE）──
constexpr int16_t  kBangBarW=10, kBangBarH=34, kBangDotH=10;  // 点在 topY+40

// ── CLAWD waiting 弹跳! + 扫弧 ──
constexpr int16_t  kWaitBangBaseY=30, kWaitBangAmp=8;        // topY 30..38
constexpr float    kWaitBangRate=0.00599f;                   // ~1.05s
constexpr int16_t  kRingR0=103, kRingR1=107, kCrestArc=40, kCrestAdv=12;
constexpr uint32_t kCrestStepMs=60;

// ── SPARKLE hero（working，中心 120,120）──
constexpr int16_t  kHeroCore=14, kHeroHalfW=7;
constexpr int16_t  kHeroLbaseMid=84, kHeroLbaseAmp=8;        // Lbase 76..92
constexpr float    kHeroPulse=0.00393f, kHeroShimmer=0.004f;
constexpr float    kHeroRotRate=0.0242f;                     // ~24°/s

// ── SPARKLE waiting（冻结琥珀 + 心跳 + !）──
constexpr int16_t  kWaitSpkCore=16, kWaitSpkHalfW=8;
constexpr float    kWaitSpkRot=22.5f;   // 转 22.5° → 无射线正上方，让开 "!" 列（修 spec §5.2 几何误判）
constexpr int16_t  kBeatLmin=64, kBeatLmax=78;
constexpr uint32_t kBeatMs=1400, kBeatUpMs=120, kBeatDownMs=200;
constexpr int16_t  kSWaitBangBaseY=14, kSWaitBangAmp=4;      // topY 14..18（顶 r=106，risk #3 勿降）
constexpr float    kSWaitBangRate=0.006f;

// ── SPARKLE idle（小静星 + 回归慢眨眼）──
constexpr int16_t  kStarCx=120, kStarCy=78, kStarCore=5, kStarHalfW=2;
constexpr int16_t  kStarLmid=13, kStarLamp=2;                // 11..15
constexpr float    kStarRate=0.00224f;
constexpr int16_t  kSIdleEyeYT=104;
constexpr int16_t  kSIdleClrLX=40, kSIdleClrRX=166, kSIdleClrY=102, kSIdleClrW=34, kSIdleClrH=64;
constexpr uint32_t kSIdleBlinkGapMin=3500, kSIdleBlinkGapMax=6000;
constexpr uint32_t kSIdleBlinkDurMin=280, kSIdleBlinkDurMax=340;

inline uint32_t randRange(uint32_t lo, uint32_t hi) {
    return lo + (esp_random() % (hi - lo + 1));
}
inline int16_t roundf16(float v) { return static_cast<int16_t>(lroundf(v)); }
inline float easeOutQuad(float t)   { return 1.0f - (1.0f - t) * (1.0f - t); }
inline float easeInOutSine(float t) { return 0.5f - 0.5f * cosf(3.14159265f * t); }

// speed → idle 节奏倍数（risk #11：只作用 idle 眨/瞟，分子，/2 归一）
inline uint32_t speedScaleNum(uint8_t s) {
    switch (s) { case 1: return 1; case 3: return 4; default: return 2; }
}

// 把一个 RGB565 写到 fb_ 的调色板索引 idx（5/6/5 → 8/8/8 位复制扩展，无歧义）。
inline void setPal565(M5Canvas& fb, uint8_t idx, uint16_t c) {
    const uint8_t r5 = (c >> 11) & 0x1F, g6 = (c >> 5) & 0x3F, b5 = c & 0x1F;
    fb.setPaletteColor(idx, static_cast<uint8_t>((r5 << 3) | (r5 >> 2)),
                            static_cast<uint8_t>((g6 << 2) | (g6 >> 4)),
                            static_cast<uint8_t>((b5 << 3) | (b5 >> 2)));
}

// 环形弧段（处理跨 360 回绕，risk #5）。画到给定缓冲（fb_）。
void fillArcWrap(M5Canvas& d, int16_t r0, int16_t r1, int16_t startDeg, int16_t widthDeg, uint8_t col) {
    int16_t s = ((startDeg % 360) + 360) % 360;
    int16_t e = s + widthDeg;
    if (e <= 360) d.fillArc(kCx, kCy, r0, r1, s, e, col);
    else { d.fillArc(kCx, kCy, r0, r1, s, 360, col);
           d.fillArc(kCx, kCy, r0, r1, 0, e - 360, col); }
}

}  // namespace

// ───────────────────────── 共享 helper ─────────────────────────
// 全部画进 fb_（离屏）；col 形参传入的是调色板索引（见上方常量）。

void ClaudeStatus::drawEye(int16_t x, int16_t y, int16_t w, int16_t h,
                           int16_t lidTop, int16_t lidBot, bool glint) {
    auto& d = fb_;
    d.fillRect(x, y, w, h, kEyeColor);                       // 眼体
    if (lidTop > 0) d.fillRect(x, y, w, lidTop, kBg);        // 上眼睑
    if (lidBot > 0) d.fillRect(x, y + h - lidBot, w, lidBot, kBg);  // 下眼睑
    if (glint && lidTop < h / 3) d.fillRect(x + w - 9, y + lidTop + 5, 4, 4, kGlint);
}

void ClaudeStatus::drawBang(int16_t cx, int16_t topY, uint16_t col) {
    auto& d = fb_;
    d.fillRect(cx - 5, topY,      kBangBarW, kBangBarH, col);   // 竖杆
    d.fillRect(cx - 5, topY + 40, kBangBarW, kBangDotH, col);   // 点
}

void ClaudeStatus::drawZ(int16_t x, int16_t y, int16_t s, uint16_t col) {
    auto& d = fb_;
    d.fillRect(x, y, s, 2, col);                       // 顶横
    d.drawLine(x + s,     y, x,     y + s, col);        // 斜（非 AA，避免调色板失真）
    d.drawLine(x + s - 1, y, x - 1, y + s, col);        // +1px 加粗成"块斜"
    d.fillRect(x, y + s - 2, s, 2, col);               // 底横
}

int16_t ClaudeStatus::drawSparkle(int16_t cx, int16_t cy, const int16_t L[8],
                                  int16_t halfW, float rotDeg, int16_t coreR,
                                  uint16_t body, uint16_t hi) {
    auto& d = fb_;
    int16_t maxL = 0;
    for (int i = 0; i < 8; i++) {
        const float a = (45.0f * i + rotDeg) * kDeg2Rad;
        const float p = a + kHalfPi;
        const int16_t tx = cx + roundf16(L[i] * cosf(a));
        const int16_t ty = cy + roundf16(L[i] * sinf(a));
        const int16_t b1x = cx + roundf16(coreR * cosf(a) + halfW * cosf(p));
        const int16_t b1y = cy + roundf16(coreR * sinf(a) + halfW * sinf(p));
        const int16_t b2x = cx + roundf16(coreR * cosf(a) - halfW * cosf(p));
        const int16_t b2y = cy + roundf16(coreR * sinf(a) - halfW * sinf(p));
        d.fillTriangle(b1x, b1y, b2x, b2y, tx, ty, body);
        if (L[i] > maxL) maxL = L[i];
    }
    d.fillCircle(cx, cy, coreR, body);
    if (coreR >= 4) d.fillCircle(cx, cy, coreR - 3, hi);
    return maxL;
}

void ClaudeStatus::eraseSparkle(int16_t cx, int16_t cy, int16_t eraseR) {
    if (eraseR > 0) fb_.fillCircle(cx, cy, eraseR, kBg);
}

int16_t ClaudeStatus::blinkLid(uint32_t now, uint32_t gapMin, uint32_t gapMax,
                               uint32_t durMin, uint32_t durMax) {
    if (!blinking_) {
        if (now < next_blink_ms_) return 0;
        blinking_    = true;
        blink_start_ = now;
        blink_dur_   = randRange(durMin, durMax);
        return 0;
    }
    const uint32_t e = now - blink_start_;
    if (e >= blink_dur_) {                       // 眨眼结束 → 排下次（risk #9：每事件重置）
        blinking_      = false;
        next_blink_ms_ = now + randRange(gapMin, gapMax);
        return 0;
    }
    const uint32_t half = blink_dur_ / 2;        // 对称三角 0→H→0
    return (e < half) ? static_cast<int16_t>(kEyeH * e / half)
                      : static_cast<int16_t>(kEyeH * (blink_dur_ - e) / (blink_dur_ - half));
}

// ───────────────────────── 调色板 ─────────────────────────

void ClaudeStatus::applyPalette() {
    setPal565(fb_, kBg,       bg_color_);   // 背景：运行时可变
    setPal565(fb_, kEyeColor, 0x0000);
    setPal565(fb_, kGreen,    0x07E0);
    setPal565(fb_, kGreenHi,  0x9FF3);
    setPal565(fb_, kAmber,    0xFC60);
    setPal565(fb_, kAmberHi,  0xFFE0);
    setPal565(fb_, kCream,    0xFF38);
    setPal565(fb_, kGlint,    0xFFFF);
    setPal565(fb_, kZFade,    0xFEF0);
}

// ───────────────────────── 静态层 ─────────────────────────

void ClaudeStatus::redrawBase() {
    if (!fb_ready_) return;
    auto& d = fb_;
    const uint32_t now = millis();
    d.fillSprite(kBg);

    // 复位所有动效追踪（spec §3）
    blinking_ = false;  blink_dur_ = kBlinkDurMin;  next_blink_ms_ = now + randRange(kBlinkGapMin, kBlinkGapMax);
    glance_dir_ = 0;    next_glance_ms_ = now + randRange(kGlanceGapMin, kGlanceGapMax);
    dozing_ = false;    z_born_ = 0;  next_z_ms_ = now + kZSpawnMs;  prev_zx_ = prev_zy_ = prev_zs_ = -1;
    prev_dy_ = 0x7FFF;  prev_lid_ = -1;  prev_dx_ = 0x7FFF;
    eraseR_ = 0;        prev_topY_ = -1;  sweep_deg_ = 0;  next_crest_ms_ = now;  prev_beatL_ = -1;
    last_step_ = now;

    if (shown_style_ == state::CcStyle::CLAWD) {
        switch (shown_status_) {
            case state::CcStatus::WORKING:
                drawEye(kLeftX,  kEyeYT, kEyeW, kEyeH, kWorkLidTop, kWorkLidBot, false);
                drawEye(kRightX, kEyeYT, kEyeW, kEyeH, kWorkLidTop, kWorkLidBot, false);
                break;
            case state::CcStatus::WAITING:
                drawEye(kWideLeftX,  kWideYT, kWideW, kWideH, 0, 14, true);
                drawEye(kWideRightX, kWideYT, kWideW, kWideH, 0, 14, true);
                d.fillArc(kCx, kCy, kRingR0, kRingR1, 0, 360, kAmber);   // 底环
                drawBang(kCx, kWaitBangBaseY, kAmber);
                break;
            default:  // IDLE：双开眼，z/眨/呼吸由 tick
                drawEye(kLeftX,  kEyeYT, kEyeW, kEyeH, 0, 0, false);
                drawEye(kRightX, kEyeYT, kEyeW, kEyeH, 0, 0, false);
                break;
        }
    } else {  // SPARKLE
        if (shown_status_ == state::CcStatus::IDLE) {
            drawEye(kLeftX,  kSIdleEyeYT, kEyeW, kEyeH, 0, 0, false);
            drawEye(kRightX, kSIdleEyeYT, kEyeW, kEyeH, 0, 0, false);
        }
        // working/waiting/idle 的星芒首帧交给 tick（33/66ms 内出现，不可见空窗）
    }
    fb_.pushSprite(0, 0);
    dirty_ = false;
}

// ───────────────────────── 生命周期 ─────────────────────────

void ClaudeStatus::onEnter() {
    if (canvas_) canvas_->releaseSprite();  // 释放 Canvas 115KB（否则轮询经 Canvas / cc 自动切入时本 mode 28KB OOM → 黑屏，§9）
    speed_        = state::g_state.speed;
    bg_color_     = state::g_state.bg_color_565;
    shown_status_ = state::g_state.cc_status;
    shown_style_  = state::g_state.cc_style;
    enter_time_   = millis();

    if (!fb_ready_) {
        fb_.setPsram(false);          // 无 PSRAM，显式落内部 SRAM（28KB）
        fb_.setColorDepth(4);         // palette_4bit → createSprite 自动建 16 槽调色板
        fb_ready_ = (fb_.createSprite(240, 240) != nullptr);
        if (fb_ready_) {
            applyPalette();
            Serial.printf("[claude_status] fb 4bpp ok, freeheap=%u\n", ESP.getFreeHeap());
        } else {
            Serial.println("[claude_status] fb createSprite FAILED -> direct fallback");
        }
    } else {
        applyPalette();               // 离开期间 bg 可能改了，重设调色板（含 kBg）
    }

    if (fb_ready_) redrawBase();
    else           M5Dial.Display.fillScreen(bg_color_);   // 退化：静态底色，无动画
}

void ClaudeStatus::onExit() {
    // v0.3.0：退出即释放 28KB（轮询经 Canvas 115KB 后 SRAM 紧张，避免共存 OOM）；
    // onEnter 惰性重建，redraw 从 g_state 无损还原。
    if (fb_ready_) { fb_.deleteSprite(); fb_ready_ = false; }
}

void ClaudeStatus::tick(uint32_t now_ms) {
    if (!fb_ready_) return;
    // 帧率门控（risk #8）：idle 66ms / 其余 33ms；动画相位由绝对 now 驱动，与帧率解耦
    const uint32_t frame_ms = (shown_status_ == state::CcStatus::IDLE) ? 66 : 33;
    if (now_ms - last_step_ < frame_ms) return;
    last_step_ = now_ms;

    dirty_ = false;
    if (shown_style_ == state::CcStyle::CLAWD) {
        switch (shown_status_) {
            case state::CcStatus::WORKING: tickClawdWorking(now_ms); break;
            case state::CcStatus::WAITING: tickClawdWaiting(now_ms); break;
            default:                       tickClawdIdle(now_ms);    break;
        }
    } else {
        switch (shown_status_) {
            case state::CcStatus::WORKING: tickSparkleWorking(now_ms); break;
            case state::CcStatus::WAITING: tickSparkleWaiting(now_ms); break;
            default:                       tickSparkleIdle(now_ms);    break;
        }
    }
    // 一帧绘制完毕，仅在有改动时一次性推屏（擦+画都在离屏，面板只收成品 → 无闪烁）
    if (dirty_) fb_.pushSprite(0, 0);
}

// ───────────────────────── CLAWD ─────────────────────────

void ClaudeStatus::tickClawdIdle(uint32_t now) {
    auto& d = fb_;
    const uint32_t elapsed = now - enter_time_;
    const bool doze = (elapsed >= kDozeAfterMs);
    dozing_ = doze;

    // 呼吸（替代水平摇摆）
    const float bAmp  = doze ? 2.0f : static_cast<float>(kBreathAmp);
    const float bRate = doze ? 0.0009f : kBreathRate;
    int16_t dy = roundf16(bAmp * sinf(now * bRate));
    int16_t lidTop = 0, dx = 0;

    if (doze) {
        lidTop = kDozeLidTop;          // 半阖
        dy    += kDozeSink;
    } else {
        // 随机眨眼（speed 只缩放间隔，risk #11）
        const uint32_t sc = speedScaleNum(speed_);
        lidTop = blinkLid(now, kBlinkGapMin * sc / 2, kBlinkGapMax * sc / 2, kBlinkDurMin, kBlinkDurMax);
        // 一次性瞟（唯一水平动）
        if (!blinking_) {
            if (glance_dir_ == 0 && now >= next_glance_ms_) {
                glance_dir_  = (esp_random() & 1) ? 1 : -1;
                glance_start_= now;
            }
            if (glance_dir_ != 0) {
                if (now - glance_start_ >= kGlanceHold) {
                    glance_dir_     = 0;
                    next_glance_ms_ = now + randRange(kGlanceGapMin * sc / 2, kGlanceGapMax * sc / 2);
                } else {
                    dx = glance_dir_ * kGlanceDx;
                }
            }
        }
    }

    // 眼：变化才重绘
    if (dy != prev_dy_ || lidTop != prev_lid_ || dx != prev_dx_) {
        d.fillRect(kIdleClrLX, kIdleClrY, kIdleClrW, kIdleClrH, kBg);
        d.fillRect(kIdleClrRX, kIdleClrY, kIdleClrW, kIdleClrH, kBg);
        drawEye(kLeftX  + dx, kEyeYT + dy, kEyeW, kEyeH, lidTop, 0, false);
        drawEye(kRightX + dx, kEyeYT + dy, kEyeW, kEyeH, lidTop, 0, false);
        prev_dy_ = dy; prev_lid_ = lidTop; prev_dx_ = dx;
        dirty_ = true;
    }

    // 打盹飘 z（仅 doze）
    if (doze) {
        if (z_born_ == 0 && now >= next_z_ms_) z_born_ = now;
        if (z_born_ != 0) {
            const uint32_t ze = now - z_born_;
            if (ze >= kZLifeMs) {                       // z 消亡
                if (prev_zs_ > 0) { d.fillRect(prev_zx_ - 1, prev_zy_ - 1, prev_zs_ + 3, prev_zs_ + 3, kBg); dirty_ = true; }
                prev_zx_ = prev_zy_ = prev_zs_ = -1;
                z_born_   = 0;
                next_z_ms_= now + kZSpawnMs;
            } else {
                const float t  = static_cast<float>(ze) / kZLifeMs;
                const int16_t zx = kZStartX + roundf16((kZEndX - kZStartX) * t);
                const int16_t zy = kZStartY + roundf16((kZEndY - kZStartY) * t);
                const int16_t zs = kZSizeMin + roundf16((kZSizeMax - kZSizeMin) * t);
                const uint8_t col = (t < 0.33f) ? kCream : (t < 0.66f) ? kZFade : kBg;  // 3 段淡出
                if (zx != prev_zx_ || zy != prev_zy_ || zs != prev_zs_) {
                    if (prev_zs_ > 0) d.fillRect(prev_zx_ - 1, prev_zy_ - 1, prev_zs_ + 3, prev_zs_ + 3, kBg);
                    if (col != kBg) drawZ(zx, zy, zs, col);
                    prev_zx_ = zx; prev_zy_ = zy; prev_zs_ = zs;
                    dirty_ = true;
                }
            }
        }
    }
}

void ClaudeStatus::tickClawdWorking(uint32_t now) {
    auto& d = fb_;
    // (a) 顶绿星芒：脉冲 + 慢转（取代通用绕圈点）
    const int16_t L = kWSpkLmid + roundf16(kWSpkLamp * sinf(now * kWSpkPulse));  // 16..20
    int16_t Larr[8]; for (auto& v : Larr) v = L;
    const float rot = fmodf(now * kWSpkRotRate, 360.0f);
    eraseSparkle(kWSpkCx, kWSpkCy, eraseR_);
    eraseR_ = drawSparkle(kWSpkCx, kWSpkCy, Larr, kWSpkHalfW, rot, kWSpkCore, kGreen, kGreenHi)
              + kWSpkHalfW + 2;
    dirty_ = true;

    // (b) 罕见专注眨眼（在下垂基线上叠加）
    const int16_t bl = blinkLid(now, kFocusBlinkGapMin, kFocusBlinkGapMax, kBlinkDurMin, kBlinkDurMin + 60);
    const int16_t lidTop = (bl > kWorkLidTop) ? bl : kWorkLidTop;
    if (lidTop != prev_lid_) {
        d.fillRect(kWorkClrLX, kWorkClrY, kWorkClrW, kWorkClrH, kBg);
        d.fillRect(kWorkClrRX, kWorkClrY, kWorkClrW, kWorkClrH, kBg);
        drawEye(kLeftX,  kEyeYT, kEyeW, kEyeH, lidTop, kWorkLidBot, false);
        drawEye(kRightX, kEyeYT, kEyeW, kEyeH, lidTop, kWorkLidBot, false);
        prev_lid_ = lidTop;
    }
}

void ClaudeStatus::tickClawdWaiting(uint32_t now) {
    auto& d = fb_;
    // (a) 弹跳 "!"（暖琥珀，中心列 x∈[113,127]，与眼不同列）
    const int16_t topY = kWaitBangBaseY + roundf16(kWaitBangAmp * fabsf(sinf(now * kWaitBangRate)));
    if (topY != prev_topY_) {
        if (prev_topY_ >= 0) d.fillRect(113, prev_topY_ - 2, 14, 56, kBg);
        drawBang(kCx, topY, kAmber);
        prev_topY_ = topY;
        dirty_ = true;
    }
    // (b) 扫过的亮弧 crest（替代 on/off 频闪；尾随 12° 抹回底色）
    if (now >= next_crest_ms_) {
        next_crest_ms_ = now + kCrestStepMs;
        fillArcWrap(d, kRingR0, kRingR1, sweep_deg_, kCrestAdv, kAmber);        // 抹尾
        sweep_deg_ = (sweep_deg_ + kCrestAdv) % 360;
        fillArcWrap(d, kRingR0, kRingR1, sweep_deg_, kCrestArc, kAmberHi);      // 画新冠
        dirty_ = true;
    }
}

// ───────────────────────── SPARKLE ─────────────────────────

void ClaudeStatus::tickSparkleWorking(uint32_t now) {
    // 大星芒：慢转 + 逐射线错相微颤
    const int16_t Lbase = kHeroLbaseMid + roundf16(kHeroLbaseAmp * sinf(now * kHeroPulse));  // 76..92
    int16_t Larr[8];
    for (int i = 0; i < 8; i++)
        Larr[i] = roundf16(Lbase * (0.92f + 0.08f * sinf(now * kHeroShimmer + i * 0.785f)));
    const float rot = fmodf(now * kHeroRotRate, 360.0f);
    eraseSparkle(kCx, kCy, eraseR_);
    eraseR_ = drawSparkle(kCx, kCy, Larr, kHeroHalfW, rot, kHeroCore, kGreen, kGreenHi)
              + kHeroHalfW + 2;
    dirty_ = true;
}

void ClaudeStatus::tickSparkleWaiting(uint32_t now) {
    auto& d = fb_;
    // (a) 冻结琥珀星芒 + 心跳放大（仅 L 变化时重绘，间歇静止无闪）
    const uint32_t tc = (now - enter_time_) % kBeatMs;
    int16_t L;
    if (tc < kBeatUpMs)                 L = kBeatLmin + roundf16((kBeatLmax - kBeatLmin) * easeOutQuad((float)tc / kBeatUpMs));
    else if (tc < kBeatUpMs + kBeatDownMs) L = kBeatLmax - roundf16((kBeatLmax - kBeatLmin) * easeInOutSine((float)(tc - kBeatUpMs) / kBeatDownMs));
    else                                L = kBeatLmin;
    bool sparkle_redrawn = false;
    if (L != prev_beatL_) {
        int16_t Larr[8]; for (auto& v : Larr) v = L;
        eraseSparkle(kCx, kCy, eraseR_);
        eraseR_ = drawSparkle(kCx, kCy, Larr, kWaitSpkHalfW, kWaitSpkRot, kWaitSpkCore, kAmber, kAmberHi)
                  + kWaitSpkHalfW + 2;
        prev_beatL_ = L;
        sparkle_redrawn = true;
        dirty_ = true;
    }
    // (b) 上方弹跳 "!"（topY 14..18，顶 r=106，risk #3 勿降）
    // 心跳擦盘 r≤88 会吞掉 "!" 下半截（y≥32）；故星芒一旦重绘，"!" 必须叠回，
    // 否则 topY 静止那几帧 "!" 半残留 → 刷新一半的闪烁。位置未变时免擦直接重绘（幂等）。
    const int16_t topY = kSWaitBangBaseY + roundf16(kSWaitBangAmp * fabsf(sinf(now * kSWaitBangRate)));
    if (topY != prev_topY_ || sparkle_redrawn) {
        if (topY != prev_topY_ && prev_topY_ >= 0) d.fillRect(113, prev_topY_ - 2, 14, 56, kBg);
        drawBang(kCx, topY, kAmberHi);
        prev_topY_ = topY;
        dirty_ = true;
    }
}

void ClaudeStatus::tickSparkleIdle(uint32_t now) {
    auto& d = fb_;
    // (a) 小静星轻闪（居上，盘底 y97 < 眼顶 y104，互不相扰）
    const int16_t L = kStarLmid + roundf16(kStarLamp * sinf(now * kStarRate));  // 11..15
    int16_t Larr[8]; for (auto& v : Larr) v = L;
    eraseSparkle(kStarCx, kStarCy, eraseR_);
    eraseR_ = drawSparkle(kStarCx, kStarCy, Larr, kStarHalfW, 0.0f, kStarCore, kCream, kAmberHi)
              + kStarHalfW + 2;
    dirty_ = true;
    // (b) 回归慢眨眼（下方）
    const int16_t lidTop = blinkLid(now, kSIdleBlinkGapMin, kSIdleBlinkGapMax, kSIdleBlinkDurMin, kSIdleBlinkDurMax);
    if (lidTop != prev_lid_) {
        d.fillRect(kSIdleClrLX, kSIdleClrY, kSIdleClrW, kSIdleClrH, kBg);
        d.fillRect(kSIdleClrRX, kSIdleClrY, kSIdleClrW, kSIdleClrH, kBg);
        drawEye(kLeftX,  kSIdleEyeYT, kEyeW, kEyeH, lidTop, 0, false);
        drawEye(kRightX, kSIdleEyeYT, kEyeW, kEyeH, lidTop, 0, false);
        prev_lid_ = lidTop;
    }
}

// ───────────────────────── 状态同步 ─────────────────────────

void ClaudeStatus::applyState(const state::SharedState& s) {
    const bool bg_changed     = (s.bg_color_565 != bg_color_);
    const bool status_changed = (s.cc_status != shown_status_);
    const bool style_changed  = (s.cc_style  != shown_style_);   // risk #2
    speed_        = s.speed;
    bg_color_     = s.bg_color_565;
    shown_status_ = s.cc_status;
    shown_style_  = s.cc_style;

    if (!fb_ready_) {
        if (bg_changed) M5Dial.Display.fillScreen(bg_color_);    // 退化路径
        return;
    }
    if (bg_changed) setPal565(fb_, kBg, bg_color_);              // 调色板 kBg → 新底色
    if (status_changed || bg_changed || style_changed) {
        enter_time_ = millis();   // 动画 / doze dwell / beat 锚点从新态重启
        redrawBase();
    }
}

}  // namespace mochi
