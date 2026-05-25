// clawd-mochi-m5dial — 入口点
// 见 CLAUDE.md §9：G46 HOLD pin 必须拉高，否则板子上电后立即断电。
// 当前为脚手架最小可编译版本，未引入 M5Dial 库的显示与输入逻辑。

#include <Arduino.h>

constexpr int HOLD_PIN = 46;

void setup() {
    pinMode(HOLD_PIN, OUTPUT);
    digitalWrite(HOLD_PIN, HIGH);

    Serial.begin(115200);
    Serial.println("clawd-mochi-m5dial v0.1.0 boot");
}

void loop() {
    delay(1000);
}
