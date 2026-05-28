#include "eyes_normal.h"
#include "../state.h"

#include <M5Dial.h>

namespace mochi {

namespace {

// 眼睛几何（圆屏适配）
constexpr int16_t kEyeW   = 30;
constexpr int16_t kEyeH   = 60;
constexpr int16_t kEyeGap = 96;
constexpr int16_t kEyeOY  = 20;

constexpr int16_t kCx    = 120;
constexpr int16_t kCy    = 120;
constexpr int16_t kEyeCY = kCy - kEyeOY;
constexpr int16_t kEyeYT = kEyeCY - kEyeH / 2;

constexpr int16_t kLeftX0  = (240 - 2 * kEyeW - kEyeGap) / 2;
constexpr int16_t kRightX0 = kLeftX0 + kEyeW + kEyeGap;

constexpr int8_t  kSwingMax    = 8;
constexpr int16_t kClearLeftX  = kLeftX0  - kSwingMax;
constexpr int16_t kClearRightX = kRightX0 - kSwingMax;
constexpr int16_t kClearW      = kEyeW + 2 * kSwingMax;

constexpr uint16_t kEyeColor = TFT_BLACK;

struct Frame {
    uint32_t duration_ms;
    int8_t   offset_x;
    bool     closed;
};

constexpr Frame kTimeline[] = {
    { 80,         -8, false},
    { 80,         -4, false},
    { 80,          0, false},
    { 80,          4, false},
    { 80,          8, false},
    {100,          0, true },
    { 70,          0, false},
    { 70,          0, true },
    {UINT32_MAX,   0, false},
};
constexpr uint8_t kFrameCount = sizeof(kTimeline) / sizeof(kTimeline[0]);

// 整数 speed scale，单位为半倍：返回值 / 2 = 实际倍数
// speed=1 fast → ×0.5；speed=2 normal → ×1；speed=3 slow → ×2
uint32_t speedScaleNum(uint8_t s) {
    switch (s) {
        case 1: return 1;
        case 3: return 4;
        default: return 2;
    }
}

}  // namespace

void EyesNormal::drawFrame(int8_t offset_x, bool closed) {
    auto& d = M5Dial.Display;
    d.fillRect(kClearLeftX,  kEyeYT, kClearW, kEyeH, bg_color_);
    d.fillRect(kClearRightX, kEyeYT, kClearW, kEyeH, bg_color_);
    if (!closed) {
        d.fillRect(kLeftX0  + offset_x, kEyeYT, kEyeW, kEyeH, kEyeColor);
        d.fillRect(kRightX0 + offset_x, kEyeYT, kEyeW, kEyeH, kEyeColor);
    }
}

void EyesNormal::onEnter() {
    speed_    = state::g_state.speed;
    bg_color_ = state::g_state.bg_color_565;
    M5Dial.Display.fillScreen(bg_color_);
    enter_time_ = millis();
    frame_idx_  = 0xFF;
}

void EyesNormal::onExit() {}

void EyesNormal::tick(uint32_t now_ms) {
    uint32_t elapsed   = now_ms - enter_time_;
    uint32_t accum     = 0;
    uint8_t  idx       = kFrameCount - 1;
    uint32_t scale_num = speedScaleNum(speed_);

    for (uint8_t i = 0; i < kFrameCount; i++) {
        if (kTimeline[i].duration_ms == UINT32_MAX) {
            idx = i;
            break;
        }
        uint32_t d = kTimeline[i].duration_ms * scale_num / 2;
        accum += d;
        if (elapsed < accum) {
            idx = i;
            break;
        }
    }

    if (idx != frame_idx_) {
        frame_idx_ = idx;
        drawFrame(kTimeline[idx].offset_x, kTimeline[idx].closed);
    }
}

void EyesNormal::applyState(const state::SharedState& s) {
    const bool bg_changed = (s.bg_color_565 != bg_color_);
    speed_    = s.speed;
    bg_color_ = s.bg_color_565;
    if (bg_changed) {
        M5Dial.Display.fillScreen(bg_color_);
        if (frame_idx_ < kFrameCount) {
            drawFrame(kTimeline[frame_idx_].offset_x, kTimeline[frame_idx_].closed);
        }
    }
    // speed 变更不显式重画；下一 tick 按新 scale 算 idx
}

}  // namespace mochi
