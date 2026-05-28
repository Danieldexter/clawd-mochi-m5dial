#include "canvas.h"
#include "../state.h"

#include <M5Dial.h>

namespace mochi {

namespace {

// 圆屏安全圈（CLAUDE.md §6）
constexpr int16_t kCx     = 120;
constexpr int16_t kCy     = 120;
constexpr int16_t kSafeR  = 110;
constexpr int32_t kSafeR2 = static_cast<int32_t>(kSafeR) * kSafeR;

inline bool inside(int16_t x, int16_t y) {
    int32_t dx = x - kCx;
    int32_t dy = y - kCy;
    return dx * dx + dy * dy <= kSafeR2;
}

}  // namespace

void Canvas::drawDot(int16_t x, int16_t y, uint16_t color) {
    if (!sprite_ready_) return;
    if (!inside(x, y)) return;
    sprite_.fillCircle(x, y, 2, color);
}

void Canvas::drawStroke(int16_t prev_x, int16_t prev_y,
                        int16_t x, int16_t y, uint16_t color) {
    if (!sprite_ready_) return;
    if (!inside(prev_x, prev_y) && !inside(x, y)) return;
    sprite_.drawLine(prev_x,     prev_y,     x,     y,     color);
    sprite_.drawLine(prev_x + 1, prev_y,     x + 1, y,     color);
    sprite_.drawLine(prev_x,     prev_y + 1, x,     y + 1, color);
}

void Canvas::clear(uint16_t bg_color) {
    bg_color_ = bg_color;
    if (sprite_ready_) {
        sprite_.fillSprite(bg_color);
        sprite_.pushSprite(0, 0);
    } else {
        M5Dial.Display.fillScreen(bg_color);
    }
}

void Canvas::flush() {
    if (sprite_ready_) sprite_.pushSprite(0, 0);
}

void Canvas::drawTestPattern() {
    drawStroke( 60,  75,  95,  90, TFT_RED);
    drawStroke( 95,  90, 135,  82, TFT_RED);
    drawStroke(135,  82, 170,  95, TFT_RED);

    drawStroke( 80, 135, 120, 115, TFT_BLUE);
    drawStroke(120, 115, 160, 135, TFT_BLUE);

    drawStroke( 70, 165, 100, 158, TFT_GREEN);
    drawStroke(100, 158, 140, 168, TFT_GREEN);
    drawStroke(140, 168, 170, 160, TFT_GREEN);

    drawDot( 90, 110, TFT_MAGENTA);
    drawDot(120, 105, TFT_ORANGE);
    drawDot(150, 110, TFT_CYAN);
}

void Canvas::onEnter() {
    bg_color_ = state::g_state.bg_color_565;
    if (!sprite_ready_) {
        // 首次进入：创建 sprite + 测试图案
        sprite_.setPsram(true);
        sprite_.setColorDepth(16);
        sprite_ready_ = sprite_.createSprite(240, 240);
        if (!sprite_ready_) {
            Serial.println("[canvas] sprite createSprite FAILED");
            M5Dial.Display.fillScreen(bg_color_);
            return;
        }
        sprite_.fillSprite(bg_color_);
        drawTestPattern();
    }
    // 已有 sprite：保留之前画的内容，仅推送回屏幕
    sprite_.pushSprite(0, 0);
}

void Canvas::onExit() {
    // Phase 7：保留 sprite 不释放（PSRAM 115 KB 长占无碍），便于切回时还原笔画
}

void Canvas::tick(uint32_t now_ms) {
    (void)now_ms;
}

void Canvas::applyState(const state::SharedState& s) {
    if (s.bg_color_565 != bg_color_) {
        bg_color_ = s.bg_color_565;
        if (sprite_ready_) {
            // Phase 6：bg 变化等同 clear（原项目 /redraw 与 /draw/clear 在 Canvas mode 行为一致）
            sprite_.fillSprite(bg_color_);
            sprite_.pushSprite(0, 0);
        }
    }
    // pen_color / speed Canvas 不直接用
}

}  // namespace mochi
