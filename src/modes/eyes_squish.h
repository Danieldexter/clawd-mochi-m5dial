#pragma once

#include "i_mode.h"

namespace mochi {

class EyesSquish : public IMode {
public:
    ModeId id() const override { return ModeId::SQUISH_EYES; }
    void   onEnter() override;
    void   onExit()  override;
    void   tick(uint32_t now_ms) override;
    void   applyState(const state::SharedState& s) override;

private:
    void drawFrame(bool open);

    uint32_t enter_time_ = 0;
    uint8_t  frame_idx_  = 0xFF;
    uint8_t  speed_      = 2;
    uint16_t bg_color_   = 0xFA00;
};

}  // namespace mochi
