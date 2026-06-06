#pragma once

// v0.3.0：旋转编码器封装。M5Dial.Encoder 是 PJRC 中断驱动累加位置（pins A41/B40），
// M5Dial.begin(cfg, enableEncoder=true) 已 attach 中断 → loop 轮询 read() 即可，无需自写 ISR。

namespace mochi::input::encoder {

void begin();   // 记录初始基线（在 M5Dial.begin 之后调）

// 返回自上次 poll 起的 detent 增量（顺时针 +，逆时针 -；无变化 0）。
// 内部按 config::kEncoderCountsPerDetent 折算并保留余数。
int  poll();

}  // namespace mochi::input::encoder
