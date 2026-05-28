#pragma once

#include <cstdint>

#include "modes/i_mode.h"

namespace mochi::state {

// 背光内部映射：协议层 bool → setBrightness 数值
constexpr uint8_t kBacklightOnLevel  = 200;
constexpr uint8_t kBacklightOffLevel = 0;

// 跨 mode 共享的协议真值源（single source of truth）。
// WS handler 修改这里 → 调当前 mode 的 applyState → 该 mode 决定立即重绘哪些字段。
struct SharedState {
    ModeId   current_mode      = ModeId::NORMAL_EYES;
    uint8_t  speed             = 2;       // 1=fast / 2=normal / 3=slow
    uint16_t bg_color_565      = 0xD880;  // #DA1100 默认（Eyes 红底）
    uint16_t pen_color_565     = 0x0000;  // #000000 默认（黑色，Phase 7 用）
    bool     backlight         = true;
    uint32_t last_broadcast_ms = 0;       // 占位，v0.3.0+ 节流用
};

extern SharedState g_state;

}  // namespace mochi::state
