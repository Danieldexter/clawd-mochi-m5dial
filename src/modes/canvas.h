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

    // v0.3.0：旋钮清屏。累积旋转 detent，最外圈画进度弧；满半圈(kClearDetents)清屏并返回 true。
    // 双向旋转均累积；停转由 tick() 超时归零。
    bool nudgeClear(int detents, uint32_t now_ms);

private:
    M5Canvas sprite_{&M5Dial.Display};
    uint16_t bg_color_ = 0xFFFF;  // 白
    bool     sprite_ready_ = false;

    int16_t  clear_accum_     = 0;  // 已累积清屏 detent（0..kClearDetents）
    uint32_t clear_rotate_ms_ = 0;  // 最近一次旋转时刻（tick 据此超时归零）

    void drawClearArc();            // 在最外圈画清屏进度弧（直绘 Display，不入 sprite）
};

}  // namespace mochi
