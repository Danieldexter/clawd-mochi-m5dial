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

// v0.3.0 旋钮清屏：转半圈（kClearDetents detent，16 detent/圈）填满最外圈进度弧 → 清屏；
// 停转 kClearIdleMs 自动归零。LovyanGFX fillArc：0°=3 点钟、顺时针 → 270°=12 点钟起填。
constexpr int16_t  kClearDetents    = 8;        // 半圈
constexpr uint32_t kClearIdleMs     = 800;      // 停转超时归零（ms）
constexpr int16_t  kClearRingR0     = 110;      // 进度弧内半径（贴安全圈外缘，最外圈）
constexpr int16_t  kClearRingR1     = 118;      // 进度弧外半径
constexpr int16_t  kClearArcStart   = 270;      // 12 点钟起
constexpr uint16_t kClearArcColor   = 0xFA00;   // 橙红（醒目"即将清屏"）
constexpr uint16_t kClearTrackColor = 0x39C8;   // 弧底槽深灰

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

bool Canvas::nudgeClear(int detents, uint32_t now_ms) {
    if (!sprite_ready_ || detents == 0) return false;
    clear_rotate_ms_ = now_ms;
    clear_accum_ += static_cast<int16_t>(detents > 0 ? detents : -detents);  // 双向均累积
    if (clear_accum_ >= kClearDetents) {
        clear_accum_ = 0;
        clear(bg_color_);          // fillSprite + pushSprite：整屏重推顺带擦掉进度弧
        return true;
    }
    drawClearArc();
    return false;
}

void Canvas::drawClearArc() {
    auto& d = M5Dial.Display;
    d.fillArc(kCx, kCy, kClearRingR0, kClearRingR1, 0, 360, kClearTrackColor);  // 底槽整圈
    const int16_t sweep = static_cast<int16_t>(360L * clear_accum_ / kClearDetents);
    if (sweep <= 0) return;
    const int16_t s = kClearArcStart % 360;   // 270（12 点钟）
    const int16_t e = s + sweep;              // 顺时针展开（accum<kClearDetents 故 sweep<360）
    if (e <= 360) d.fillArc(kCx, kCy, kClearRingR0, kClearRingR1, s, e, kClearArcColor);
    else {                                    // 跨 360 回绕（同 pc_monitor fillArcWrap）
        d.fillArc(kCx, kCy, kClearRingR0, kClearRingR1, s, 360, kClearArcColor);
        d.fillArc(kCx, kCy, kClearRingR0, kClearRingR1, 0, e - 360, kClearArcColor);
    }
}

void Canvas::onEnter() {
    bg_color_ = state::g_state.bg_color_565;
    clear_accum_ = 0;                 // 进入即清零清屏进度（防离场时残留）
    if (!sprite_ready_) {
        // 首次进入：创建 sprite，空白画布
        sprite_.setPsram(true);
        sprite_.setColorDepth(16);
        sprite_ready_ = sprite_.createSprite(240, 240);
        if (!sprite_ready_) {
            Serial.println("[canvas] sprite createSprite FAILED");
            M5Dial.Display.fillScreen(bg_color_);
            return;
        }
        sprite_.fillSprite(bg_color_);
    }
    // 已有 sprite：保留之前画的内容，仅推送回屏幕
    sprite_.pushSprite(0, 0);
}

void Canvas::onExit() {
    // Phase 7：保留 sprite 不释放（PSRAM 115 KB 长占无碍），便于切回时还原笔画
}

void Canvas::releaseSprite() {
    // v0.4.0：GIF_PLAYER 进入时调用——释放 115KB 让 GIF 解码缓冲有内存（二者互斥，见 CLAUDE.md §9）。
    if (sprite_ready_) {
        sprite_.deleteSprite();
        sprite_ready_ = false;
        Serial.println("[canvas] sprite released (gif_player took the big-buffer budget)");
    }
}

void Canvas::tick(uint32_t now_ms) {
    // 旋钮清屏进度：停转超时 → 归零并重推画布覆盖掉最外圈进度弧
    if (clear_accum_ > 0 && now_ms - clear_rotate_ms_ >= kClearIdleMs) {
        clear_accum_ = 0;
        flush();
    }
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
