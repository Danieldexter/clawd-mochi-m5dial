#pragma once

#include <cstdint>

// v0.3.0：触摸手势封装。M5.Touch 已自带手势状态机（touch_detail_t 的 wasClicked/wasHold/
// isDragging），无需自写识别器。每 loop（M5Dial.update 之后）调一次 poll()。

namespace mochi::input::touch {

enum class Gesture : uint8_t { None, Tap, LongPress };

struct Event {
    Gesture gesture = Gesture::None;
    bool    pressed = false;        // 原始按压态（Canvas 自由绘画用，绕开 isDragging 8px 阈值）
    int16_t x = 0, y = 0;           // 当前点
    int16_t prev_x = 0, prev_y = 0; // 上一帧点（画线用）
};

Event poll();

}  // namespace mochi::input::touch
