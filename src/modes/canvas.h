#pragma once

#include "i_mode.h"

#include <M5Dial.h>

namespace mochi {

class Canvas : public IMode {
public:
    ModeId id() const override { return ModeId::CANVAS; }
    void   onEnter() override;
    void   onExit()  override;
    void   tick(uint32_t now_ms) override;
    void   applyState(const state::SharedState& s) override;

    // 笔画 / 清屏 API（Phase 6 public 化以便 WS handler 调用；Phase 7 接 stroke 消息）
    void drawDot(int16_t x, int16_t y, uint16_t color);
    void drawStroke(int16_t prev_x, int16_t prev_y,
                    int16_t x, int16_t y, uint16_t color);
    void clear(uint16_t bg_color);
    // Phase 7：drawDot/drawStroke 只画 sprite，外部批量调用后 flush 一次推送
    void flush();

private:
    M5Canvas sprite_{&M5Dial.Display};
    uint16_t bg_color_ = 0xFFFF;  // 白
    bool     sprite_ready_ = false;

    void drawTestPattern();
};

}  // namespace mochi
