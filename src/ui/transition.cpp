#include "transition.h"

#include "../config.h"

#include <M5Dial.h>
#include <cmath>

namespace mochi::ui {

namespace {
constexpr int16_t  kCx = config::kScreenCenterX;
constexpr int16_t  kCy = config::kScreenCenterY;
constexpr uint16_t kSpark = 0xFFE0;  // 亮黄星点
}  // namespace

// 由圆心向外扩张的 bg 圆盘（径向擦掉旧画面），前缘缀 8 颗星点 → Claude 星芒爆发感。
void playSparkleWipe(uint16_t bg_color_565) {
    auto& d = M5Dial.Display;
    for (int16_t r = 8; r <= 176; r += 14) {       // 176 覆盖 240 方屏半对角（~170）含四角
        d.fillCircle(kCx, kCy, r, bg_color_565);   // 径向擦旧 → 底色
        for (int k = 0; k < 8; ++k) {              // 前缘 8 向星点
            const float a = k * 0.7853982f;        // 45° 步进
            const int16_t x = kCx + static_cast<int16_t>(cosf(a) * r);
            const int16_t y = kCy + static_cast<int16_t>(sinf(a) * r);
            d.fillCircle(x, y, 3, kSpark);
        }
        delay(12);
    }
    d.fillScreen(bg_color_565);  // 清场 → 新 mode onEnter 在干净底色上重绘
}

}  // namespace mochi::ui
