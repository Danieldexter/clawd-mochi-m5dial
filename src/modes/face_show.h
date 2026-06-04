#pragma once

#include "i_mode.h"

#include <M5Dial.h>  // M5Canvas（离屏后备缓冲类型）

namespace mochi {

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

private:
    void applyPalette();   // 调色板索引 → RGB565（kBg = 运行时 bg_color_）
    void startFace(uint8_t face_index, uint32_t now_ms);
    uint8_t frameFor(uint32_t now_ms) const;
    void redraw(uint8_t frame);  // fillSprite(kBg) + drawFace + pushSprite

    M5Canvas fb_{&M5Dial.Display};
    bool     fb_ready_      = false;
    uint8_t  shown_face_index_ = 0;
    uint8_t  shown_frame_      = 0;
    uint32_t anim_start_ms_    = 0;
    uint16_t bg_color_      = 0xFA00;
};

}  // namespace mochi
