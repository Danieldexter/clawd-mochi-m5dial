#include "claude_code.h"
#include "../state.h"

#include <M5Dial.h>
#include <cstring>

namespace mochi {

namespace {

constexpr int16_t kInscribedX = 35;
constexpr int16_t kInscribedW = 170;

constexpr uint16_t kColorOrange = 0xFD20;
constexpr uint16_t kColorGreen  = TFT_GREEN;

constexpr int16_t kTitleBarY = 38;
constexpr int16_t kSepY      = kTitleBarY + 10;

constexpr int16_t kFont0W    = 6;
constexpr int16_t kFont0H    = 8;
constexpr int16_t kRowH      = 14;
constexpr int16_t kTermYT    = 70;   // 首行 y top
constexpr int16_t kTermYB    = kTermYT + 8 * kRowH;  // 70 + 112 = 182
constexpr int16_t kPromptChars = 9;  // "clawd:~$ "

// 光标位置：紧跟"当前行 prompt + 已输入字符数"之后
inline int16_t cursorX(uint8_t col) {
    return kInscribedX + (kPromptChars + col) * kFont0W;
}
inline int16_t cursorY(uint8_t row) {
    return kTermYT + row * kRowH;
}

}  // namespace

void ClaudeCode::drawTitleBar() {
    auto& d = M5Dial.Display;
    d.setFont(&fonts::Font0);
    d.setTextDatum(top_left);
    d.setTextSize(1);
    d.setTextColor(kColorOrange, bg_color_);
    d.drawString("clawd@mochi terminal", kInscribedX, kTitleBarY);
    d.drawFastHLine(kInscribedX, kSepY, kInscribedW, kColorOrange);
}

void ClaudeCode::drawTerminalRows() {
    auto& d = M5Dial.Display;
    // 清终端区
    d.fillRect(kInscribedX, kTermYT, kInscribedW, kTermYB - kTermYT, bg_color_);

    d.setFont(&fonts::Font0);
    d.setTextDatum(top_left);
    d.setTextSize(1);

    for (uint8_t i = 0; i < kRows; i++) {
        const int16_t y = kTermYT + i * kRowH;
        d.setTextColor(kColorGreen, bg_color_);
        d.drawString("clawd:~$ ", kInscribedX, y);
        if (term_buf_[i][0] != '\0') {
            d.setTextColor(TFT_WHITE, bg_color_);
            d.drawString(term_buf_[i], kInscribedX + kPromptChars * kFont0W, y);
        }
    }
}

void ClaudeCode::drawCursor(bool visible) {
    M5Dial.Display.fillRect(cursorX(term_col_), cursorY(term_row_),
                            kFont0W, kFont0H,
                            visible ? kColorGreen : bg_color_);
}

void ClaudeCode::scrollUpIfNeeded() {
    if (term_row_ < kRows - 1) return;
    // FIFO 向上滚动一行：buf[0] 丢弃，其余上移
    for (uint8_t i = 0; i < kRows - 1; i++) {
        memcpy(term_buf_[i], term_buf_[i + 1], kColMax + 1);
    }
    memset(term_buf_[kRows - 1], 0, kColMax + 1);
    term_col_ = 0;
    // term_row_ 保持 kRows - 1
}

void ClaudeCode::inputChar(char c) {
    if (c == '\b') {
        if (term_col_ > 0) {
            term_col_--;
            term_buf_[term_row_][term_col_] = '\0';
        }
    } else if (c == '\n' || c == '\r') {
        if (term_row_ < kRows - 1) {
            term_row_++;
            term_col_ = 0;
        } else {
            scrollUpIfNeeded();
        }
    } else if (c >= 0x20 && c <= 0x7E) {
        if (term_col_ < kColMax) {
            term_buf_[term_row_][term_col_++] = c;
            term_buf_[term_row_][term_col_]   = '\0';
        }
    }
    // 其它控制字符（tab / ESC / 等）忽略

    drawTerminalRows();
    cursor_visible_ = true;  // 下一帧 tick 会自然切换
    drawCursor(true);
}

void ClaudeCode::onEnter() {
    bg_color_ = state::g_state.bg_color_565 == 0xD880 ? TFT_BLACK : state::g_state.bg_color_565;
    // 终端审美默认黑底；如果 SharedState 是 Eyes 红色（0xD880）这里强制改回黑，
    // 让用户切到 Claude Code 看到正确的终端配色。Web 改 bg_color 还是按 SharedState 同步。

    M5Dial.Display.fillScreen(bg_color_);
    drawTitleBar();
    // 清空终端缓冲（Phase 6 切回 ClaudeCode 重新开始）
    memset(term_buf_, 0, sizeof(term_buf_));
    term_row_ = 0;
    term_col_ = 0;
    drawTerminalRows();

    enter_time_     = millis();
    cursor_visible_ = false;
}

void ClaudeCode::onExit() {}

void ClaudeCode::tick(uint32_t now_ms) {
    const bool should_show = ((now_ms - enter_time_) / 500) % 2 == 0;
    if (should_show != cursor_visible_) {
        cursor_visible_ = should_show;
        drawCursor(should_show);
    }
}

void ClaudeCode::applyState(const state::SharedState& s) {
    if (s.bg_color_565 != bg_color_ && s.bg_color_565 != 0xD880) {
        bg_color_ = s.bg_color_565;
        M5Dial.Display.fillScreen(bg_color_);
        drawTitleBar();
        drawTerminalRows();
        drawCursor(cursor_visible_);
    }
}

}  // namespace mochi
