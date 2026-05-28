#include "eyes_squish.h"
#include "../state.h"

#include <M5Dial.h>

namespace mochi {

namespace {

constexpr int16_t kCx       = 120;
constexpr int16_t kCy       = 120;
constexpr int16_t kEyeCY    = 100;

constexpr int16_t kHalfW    = 12;
constexpr int16_t kHalfH    = 12;

constexpr int16_t kLeftCX   = 72;
constexpr int16_t kRightCX  = 168;

constexpr int16_t kPad       = 2;
constexpr int16_t kLeftBoxX  = kLeftCX  - kHalfW - kPad;
constexpr int16_t kRightBoxX = kRightCX - kHalfW - kPad;
constexpr int16_t kBoxW      = kHalfW * 2 + 2 * kPad;
constexpr int16_t kBoxYT     = kEyeCY  - kHalfH - kPad;
constexpr int16_t kBoxH      = kHalfH * 2 + 2 * kPad;

constexpr uint16_t kEyeColor = TFT_BLACK;

struct Frame {
    uint32_t duration_ms;
    bool     open;
};

constexpr Frame kTimeline[] = {
    {160, true },
    {100, false},
    {160, true },
    {100, false},
    {160, true },
    {100, false},
    {UINT32_MAX, true},
};
constexpr uint8_t kFrameCount = sizeof(kTimeline) / sizeof(kTimeline[0]);

uint32_t speedScaleNum(uint8_t s) {
    switch (s) {
        case 1: return 1;
        case 3: return 4;
        default: return 2;
    }
}

}  // namespace

void EyesSquish::drawFrame(bool open) {
    auto& d = M5Dial.Display;
    d.fillRect(kLeftBoxX,  kBoxYT, kBoxW, kBoxH, bg_color_);
    d.fillRect(kRightBoxX, kBoxYT, kBoxW, kBoxH, bg_color_);
    if (open) {
        d.drawWideLine(kLeftCX - kHalfW, kEyeCY - kHalfH,
                       kLeftCX + kHalfW, kEyeCY, 1.5f, kEyeColor);
        d.drawWideLine(kLeftCX + kHalfW, kEyeCY,
                       kLeftCX - kHalfW, kEyeCY + kHalfH, 1.5f, kEyeColor);
        d.drawWideLine(kRightCX + kHalfW, kEyeCY - kHalfH,
                       kRightCX - kHalfW, kEyeCY, 1.5f, kEyeColor);
        d.drawWideLine(kRightCX - kHalfW, kEyeCY,
                       kRightCX + kHalfW, kEyeCY + kHalfH, 1.5f, kEyeColor);
    } else {
        d.drawWideLine(kLeftCX  - kHalfW, kEyeCY,
                       kLeftCX  + kHalfW, kEyeCY, 1.5f, kEyeColor);
        d.drawWideLine(kRightCX - kHalfW, kEyeCY,
                       kRightCX + kHalfW, kEyeCY, 1.5f, kEyeColor);
    }
}

void EyesSquish::onEnter() {
    speed_    = state::g_state.speed;
    bg_color_ = state::g_state.bg_color_565;
    M5Dial.Display.fillScreen(bg_color_);
    enter_time_ = millis();
    frame_idx_  = 0xFF;
}

void EyesSquish::onExit() {}

void EyesSquish::tick(uint32_t now_ms) {
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
        drawFrame(kTimeline[idx].open);
    }
}

void EyesSquish::applyState(const state::SharedState& s) {
    const bool bg_changed = (s.bg_color_565 != bg_color_);
    speed_    = s.speed;
    bg_color_ = s.bg_color_565;
    if (bg_changed) {
        M5Dial.Display.fillScreen(bg_color_);
        if (frame_idx_ < kFrameCount) {
            drawFrame(kTimeline[frame_idx_].open);
        }
    }
}

}  // namespace mochi
