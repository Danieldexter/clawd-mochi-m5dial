#pragma once

#include "i_mode.h"

#include <cstdint>

namespace mochi {

class ClaudeCode : public IMode {
public:
    ModeId id() const override { return ModeId::CLAUDE_CODE; }
    void   onEnter() override;
    void   onExit()  override;
    void   tick(uint32_t now_ms) override;
    void   applyState(const state::SharedState& s) override;

    // Phase 6 终端键盘输入（来自 WS 单字符）
    // 支持：printable ASCII / '\b' 退格 / '\n' 换行；其余忽略
    void inputChar(char c);

private:
    static constexpr uint8_t kRows   = 8;
    static constexpr uint8_t kColMax = 19;  // 28 列 − 9 列 prompt = 19 列用户输入

    char    term_buf_[kRows][kColMax + 1] = {};
    uint8_t term_row_ = 0;   // 当前写入行
    uint8_t term_col_ = 0;   // 当前列（不含 prompt）

    uint32_t enter_time_     = 0;
    bool     cursor_visible_ = false;
    uint16_t bg_color_       = 0x0000;  // 黑

    void drawTitleBar();
    void drawTerminalRows();
    void drawCursor(bool visible);
    void scrollUpIfNeeded();
};

}  // namespace mochi
