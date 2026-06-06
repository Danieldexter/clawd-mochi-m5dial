#pragma once

#include <cstdint>

// v0.3.0：切 mode 时的 Claude 星芒径向擦除转场（§6 认可径向转场）。
// 直绘 Display，一次性同步 ~150ms（类比 reminder/QR 屏既有同步绘制；非动画期短阻塞可接受）。
// 调用时机：旧 mode onExit 之后、新 mode onEnter 之前。

namespace mochi::ui {

void playSparkleWipe(uint16_t bg_color_565);

}  // namespace mochi::ui
