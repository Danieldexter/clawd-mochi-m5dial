#include "touch.h"

#include <M5Dial.h>

namespace mochi::input::touch {

Event poll() {
    Event ev;
    const auto& d = M5Dial.Touch.getDetail();
    ev.x = d.x;       ev.y = d.y;
    ev.prev_x = d.prev_x; ev.prev_y = d.prev_y;

    ev.pressed = d.isPressed();  // Canvas 用原始按压态连续描画（isDragging 8px 阈值对细笔太粗）
    // 手势：长按（hold 起始一次性）> 轻点（释放）。拖动不再走手势——Canvas 自走 pressed。
    if      (d.wasHold())    ev.gesture = Gesture::LongPress;
    else if (d.wasClicked()) ev.gesture = Gesture::Tap;
    return ev;
}

}  // namespace mochi::input::touch
