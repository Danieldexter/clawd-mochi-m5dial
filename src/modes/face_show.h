#pragma once

#include "i_mode.h"

#include <M5Dial.h>  // M5Canvas（离屏后备缓冲类型）

namespace mochi {

class Canvas;  // 前置声明：onEnter 调 canvas_->releaseSprite() 腾出大缓冲预算（与 Canvas 115KB 互斥，§9）

// Phase 12：Face System 展示 mode —— 按 g_state.face_index 渲染 faces_data 注册表。
// 沿用 claude_status 的 4bpp 离屏后备缓冲套路（本板无 PSRAM，省 SRAM 与
// Canvas 的 115KB 共存）；动画用 tick() 非阻塞播放，每帧仅一次 pushSprite。
class FaceShow : public IMode {
public:
    ModeId id() const override { return ModeId::FACE_SHOW; }
    void   onEnter() override;
    void   onExit()  override;
    void   tick(uint32_t now_ms) override;
    void   applyState(const state::SharedState& s) override;

    // v0.3.0：触摸 tap → 临时显示反应脸（wink）~1s 后自动回当前真值脸（poke Clawd）。
    void   react(uint32_t now_ms);
    // v0.3.0：自动轮换强制重播 g_state.face_index 当前表情（抽中同一张也从头播）。
    void   restart(uint32_t now_ms);

    // v0.4.0：main setup 注入 Canvas。onEnter 调 canvas_->releaseSprite() 释放其 115KB——否则
    // web 直跳 Canvas→Faces 时 115KB 仍驻留，28KB createSprite OOM → 黑屏（CLAUDE.md §9）。
    void   attachCanvas(Canvas* c) { canvas_ = c; }

private:
    void applyPalette();   // 调色板索引 → RGB565（kBg = 运行时 bg_color_）
    void startFace(uint8_t face_index, uint32_t now_ms);
    uint8_t frameFor(uint32_t now_ms) const;
    void redraw(uint8_t frame);  // fillSprite(kBg) + drawFace/drawIdleEyes + pushSprite

    M5Canvas fb_{&M5Dial.Display};
    bool     fb_ready_      = false;
    Canvas*  canvas_        = nullptr;  // 大缓冲互斥：onEnter 释放其 115KB sprite（§9）
    uint8_t  shown_face_index_ = 0;
    uint8_t  shown_frame_      = 0;
    uint32_t anim_start_ms_    = 0;
    uint32_t react_until_ms_   = 0;   // v0.3.0：tap 反应到点时刻（0=无反应中）
    uint32_t last_idle_ms_     = 0;   // v0.3.0：anim_idle 连续重绘的帧率门控
    uint16_t bg_color_      = 0xFA00;
};

}  // namespace mochi
