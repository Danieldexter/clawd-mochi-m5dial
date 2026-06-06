#include "beeper.h"

#include <M5Dial.h>

namespace mochi::audio::beeper {

namespace {

constexpr uint8_t kVolume = 90;  // 0..255，M5Dial 蜂鸣器 80dB，过响可下调（HW 标定）

// 单音（freq Hz / dur ms）。channel -1 自动分配。
inline void note(float freq, uint32_t ms) { M5Dial.Speaker.tone(freq, ms); }

// 双音序列：第二音 stop_current_sound=false 排在第一音之后（Speaker 每通道双缓冲，wavinfo[0/1]）。
inline void chime2(float f1, uint32_t d1, float f2, uint32_t d2) {
    M5Dial.Speaker.tone(f1, d1, 0, /*stop_current=*/true);
    M5Dial.Speaker.tone(f2, d2, 0, /*stop_current=*/false);
}

}  // namespace

void begin() { M5Dial.Speaker.setVolume(kVolume); }

void tick()       { note(2600, 8); }
void modeSwitch() { note(1760, 80); }
void penDown()    { note(2200, 20); }

void clientConnect() { chime2(988, 70, 1480, 90); }   // 上扬
void pinToggle(bool pinned) {
    if (pinned) chime2(1200, 45, 1600, 70);           // 锁定：升
    else        chime2(1600, 45, 1200, 70);           // 解锁：降
}

void bootChime() { chime2(880, 90, 1320, 140); }      // 开机上扬
void attention() { chime2(1397, 80, 1864, 120); }     // 注意：升（醒目）
void success()   { chime2(1318, 80, 1976, 150); }     // 完成：升（明亮收束）
void yawn()      { chime2(1100, 130, 660, 200); }     // 哈欠：下行

}  // namespace mochi::audio::beeper
