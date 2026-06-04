#include "faces_data.h"

#include "../config.h"

#include <cmath>
#include <cstring>

namespace mochi::faces {

namespace {

constexpr int16_t kCx = config::kScreenCenterX;
constexpr int16_t kCy = config::kScreenCenterY;

// 招牌眼几何（沿用 claude_status 坐标，保视觉连续；四角已验算落 r110 安全圈内）。
// 所有表情统一用黑硬边方眼 + 眼睑遮罩 + 白 glint，与 eyes_normal / claude_status 同语言。
constexpr int16_t kEyeW = 30, kEyeH = 60, kLeftX = 42, kRightX = 168, kEyeYT = 70;

constexpr FaceSpec kFaces[kFaceCount] = {
    {"face_wuyu",      "Deadpan",   "无语",   false, 1, 1, true,  240},
    {"face_wenhao",    "Question",  "问号",   false, 1, 1, true,  240},
    {"face_gantanhao", "Alert",     "感叹号", false, 1, 1, true,  240},
    {"face_angry",     "Angry",     "生气",   false, 1, 1, true,  240},
    {"face_yes",       "Yes",       "对号",   false, 1, 1, true,  240},
    {"face_X",         "No",        "叉叉",   false, 1, 1, true,  240},
    {"face_glass",     "Glasses",   "墨镜",   false, 1, 1, true,  240},
    {"anim_jiyanjing", "Wink",      "挤眼睛", true,  3, 3, false, 180},
    {"anim_yun",       "Dizzy",     "晕晕",   true,  3, 3, false, 170},
    {"anim_close",     "Close",     "闭眼睛", true,  3, 3, false, 190},
    {"anim_dead",      "Dead",      "死掉了", true,  3, 3, true,  210},
    {"anim_dian",      "Wait",      "等等",   true,  3, 1, true,  260},
    {"anim_smile",     "Smile",     "笑笑",   true,  2, 3, false, 210},
    {"anim_look",      "Look",      "看你",   true,  3, 3, false, 220},
    {"anim_hart",      "Heart",     "心跳",   true,  2, 3, false, 210},
    {"anim_zzz",       "Sleep",     "睡着了", true,  3, 1, true,  360},
    {"anim_ganga",     "Awkward",   "尴尬",   true,  3, 1, true,  300},
};

inline uint8_t clampFrame(uint8_t face_index, uint8_t frame) {
    const auto& f = kFaces[(face_index < kFaceCount) ? face_index : 0];
    return (frame < f.frame_count) ? frame : static_cast<uint8_t>(f.frame_count - 1);
}

// ───────────────────────── 通用 primitive ─────────────────────────

void thickLine(M5Canvas& fb, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
               int16_t w, uint8_t col) {
    const int16_t r = w / 2;
    const bool shallow = abs(x1 - x0) >= abs(y1 - y0);
    for (int16_t o = -r; o <= r; ++o) {
        if (shallow) fb.drawLine(x0, y0 + o, x1, y1 + o, col);
        else         fb.drawLine(x0 + o, y0, x1 + o, y1, col);
    }
}

// 招牌眼：黑实心方眼 + 上/下眼睑（按 kBg 遮罩成各种眼形）+ 右上白 glint。
// 移植自 claude_status::drawEye（free function 版）。lidTop/lidBot=0 即全睁。
void eyeRect(M5Canvas& fb, int16_t x, int16_t y, int16_t w, int16_t h,
             int16_t lidTop, int16_t lidBot, bool glint) {
    fb.fillRect(x, y, w, h, kInk);
    if (lidTop > 0) fb.fillRect(x, y, w, lidTop, kBg);
    if (lidBot > 0) fb.fillRect(x, y + h - lidBot, w, lidBot, kBg);
    if (glint && lidTop < h / 3) fb.fillRect(x + w - 9, y + lidTop + 5, 4, 4, kWhite);
}

// 标准双眼（带左右整体位移 dx/dy；瞟 / 看 / 呼吸复用）。
void eyesPair(M5Canvas& fb, int16_t dx, int16_t dy, int16_t lidTop, int16_t lidBot, bool glint) {
    eyeRect(fb, kLeftX  + dx, kEyeYT + dy, kEyeW, kEyeH, lidTop, lidBot, glint);
    eyeRect(fb, kRightX + dx, kEyeYT + dy, kEyeW, kEyeH, lidTop, lidBot, glint);
}

// 闭眼曲线：smile=true → ‿（喜悦弯眼，lift 越大笑越深）；false → ^（睡 / 闭）。
void closedEye(M5Canvas& fb, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
               bool smile, uint8_t col = kInk, int16_t lift = 12) {
    const int16_t mid_x = (x0 + x1) / 2;
    const int16_t mid_y = smile ? y0 + lift : y0 - 6;
    thickLine(fb, x0, y0, mid_x, mid_y, 5, col);
    thickLine(fb, mid_x, mid_y, x1, y1, 5, col);
}

void drawBang(M5Canvas& fb, int16_t cx, int16_t top_y, uint8_t col) {
    fb.fillRect(cx - 5, top_y,      10, 34, col);     // 竖杆（硬方块，移植 claude_status）
    fb.fillRect(cx - 5, top_y + 40, 10, 10, col);     // 点
}

// 硬方块问号（顶横 + 右竖钩 + 回折 + 居中短竖 + 点；全 fillRect，与 ! / z 同语言）。
void drawQuestion(M5Canvas& fb, int16_t cx, int16_t top_y, uint8_t col) {
    fb.fillRect(cx - 13, top_y,      26, 9,  col);    // 顶横
    fb.fillRect(cx + 4,  top_y,      9,  20, col);    // 右竖（下钩）
    fb.fillRect(cx - 4,  top_y + 20, 17, 9,  col);    // 回折横（向中收）
    fb.fillRect(cx - 4,  top_y + 27, 9,  11, col);    // 居中短竖到尖
    fb.fillRect(cx - 4,  top_y + 46, 9,  9,  col);    // 点（下方留缝）
}

void drawCheck(M5Canvas& fb) {
    thickLine(fb, 80, 128, 108, 158, 9, kGreen);      // 短臂 ↘
    thickLine(fb, 108, 158, 166, 80, 9, kGreen);      // 长臂 ↗
}

void drawX(M5Canvas& fb, int16_t cx, int16_t cy, int16_t r, uint8_t col) {
    thickLine(fb, cx - r, cy - r, cx + r, cy + r, 7, col);
    thickLine(fb, cx + r, cy - r, cx - r, cy + r, 7, col);
}

// block z（横杆 + 块斜 + 横杆；移植 claude_status::drawZ，与 ! / ? 同硬边语言）。
void drawZ(M5Canvas& fb, int16_t x, int16_t y, int16_t s, uint8_t col) {
    fb.fillRect(x, y, s, 2, col);                     // 顶横
    fb.drawLine(x + s,     y, x,     y + s, col);     // 斜（非 AA，调色板友好）
    fb.drawLine(x + s - 1, y, x - 1, y + s, col);     // +1px 加粗成块斜
    fb.fillRect(x, y + s - 2, s, 2, col);             // 底横
}

void drawSweat(M5Canvas& fb, int16_t x, int16_t y, int16_t h) {
    fb.fillCircle(x, y + h / 3, h / 4, kCyan);
    fb.fillTriangle(x, y, x - h / 4, y + h / 2, x + h / 4, y + h / 2, kCyan);
    fb.fillCircle(x, y + h / 2, h / 5, kWhite);
}

void drawHeart(M5Canvas& fb, int16_t cx, int16_t cy, int16_t s, uint8_t col) {
    const int16_t r     = s * 3 / 8;                  // 瓣半径
    const int16_t lobeY = cy - s / 4;
    fb.fillCircle(cx - r + 2, lobeY, r, col);         // 左瓣
    fb.fillCircle(cx + r - 2, lobeY, r, col);         // 右瓣
    fb.fillTriangle(cx - 2 * r + 2, lobeY,            // 下尖（顶边对齐两瓣外缘 → 不鼓包）
                    cx + 2 * r - 2, lobeY, cx, cy + s / 2, col);
    const int16_t gr = (s >= 44) ? 4 : 3;
    fb.fillCircle(cx - r + 1, lobeY - r / 2, gr, kWhite);  // 高光
}

void drawSpiral(M5Canvas& fb, int16_t cx, int16_t cy, float phase, uint8_t col) {
    float last_x = cx;
    float last_y = cy;
    for (int i = 1; i <= 22; ++i) {
        const float t = i * 0.44f + phase;
        const float r = 2.2f * i;
        const float x = cx + cosf(t) * r;
        const float y = cy + sinf(t) * r;
        thickLine(fb, static_cast<int16_t>(lroundf(last_x)),
                  static_cast<int16_t>(lroundf(last_y)),
                  static_cast<int16_t>(lroundf(x)),
                  static_cast<int16_t>(lroundf(y)), 4, col);
        last_x = x;
        last_y = y;
    }
}

// ───────────────────────── 静态 7 张 ─────────────────────────

void drawDeadpan(M5Canvas& fb) {            // 无语 -_-：居中扁黑 slit
    eyesPair(fb, 0, 0, 26, 26, false);
}

void drawQuestionFace(M5Canvas& fb) {       // 问号 ≈ waiting 换符号（去环）：瞪大眼 + 居中琥珀 block ?
    eyeRect(fb, 38,  64, 34, 64, 0, 14, true);
    eyeRect(fb, 168, 64, 34, 64, 0, 14, true);
    drawQuestion(fb, kCx, 16, kAmber);
}

void drawAlertFace(M5Canvas& fb) {          // 感叹 ≈ waiting（去环）：瞪大眼 + 居中琥珀 block !
    eyeRect(fb, 38,  64, 34, 64, 0, 14, true);
    eyeRect(fb, 168, 64, 34, 64, 0, 14, true);
    drawBang(fb, kCx, 26, kAmber);
}

void drawAngry(M5Canvas& fb) {              // 生气：内斜粗眉 + 压窄黑眼
    thickLine(fb, 46, 78, 96, 98, 8, kInk);     // 左眉 ＼
    thickLine(fb, 194, 78, 144, 98, 8, kInk);   // 右眉 ／
    eyeRect(fb, kLeftX,  104, kEyeW, 28, 0, 6, false);
    eyeRect(fb, kRightX, 104, kEyeW, 28, 0, 6, false);
}

void drawYes(M5Canvas& fb) {                // 对号：喜悦弯眼（加深）+ 利落绿 ✓
    closedEye(fb, 50, 92, 96, 92, true, kInk, 16);
    closedEye(fb, 144, 92, 190, 92, true, kInk, 16);
    drawCheck(fb);
}

void drawNo(M5Canvas& fb) {                 // 叉叉：两黑 X 眼（红 X 橙底不可见 → 黑）
    drawX(fb, 57, 100, 22, kInk);
    drawX(fb, 183, 100, 22, kInk);
}

void drawGlasses(M5Canvas& fb) {            // 墨镜：黑硬镜片（呼应招牌眼位）+ 鼻梁桥 + 镜腿 + 白反光
    fb.fillRect(40,  84, 62, 40, kInk);          // 左镜片
    fb.fillRect(138, 84, 62, 40, kInk);          // 右镜片
    fb.fillRect(102, 96, 36, 9, kInk);           // 鼻梁桥
    thickLine(fb, 40,  90, 30,  82, 5, kInk);    // 左镜腿
    thickLine(fb, 200, 90, 210, 82, 5, kInk);    // 右镜腿（尖 x210 守安全圈）
    thickLine(fb, 52,  118, 70,  90, 5, kWhite); // 左反光
    thickLine(fb, 150, 118, 168, 90, 5, kWhite); // 右反光
}

// ───────────────────────── 动画 10 张 ─────────────────────────

void drawWink(M5Canvas& fb, uint8_t frame) {        // 挤眼：睁 → 左眼眨 → 睁大
    if (frame == 1) {
        closedEye(fb, 46, 100, 92, 100, true);
        eyeRect(fb, kRightX, kEyeYT, kEyeW, kEyeH, 0, 0, true);
    } else if (frame == 2) {
        eyeRect(fb, kLeftX,  66, kEyeW, 68, 0, 0, true);
        eyeRect(fb, kRightX, 66, kEyeW, 68, 0, 0, true);
    } else {
        eyesPair(fb, 0, 0, 0, 0, true);
    }
}

void drawDizzy(M5Canvas& fb, uint8_t frame) {       // 晕：黑螺旋眼旋转
    const float phase = frame * 1.7f;
    drawSpiral(fb, 72, 100, phase, kInk);
    drawSpiral(fb, 168, 100, phase + 3.14159f, kInk);
}

void drawClose(M5Canvas& fb, uint8_t frame) {       // 闭眼：睁 → 半闭 → 闭线
    if (frame == 0)      eyesPair(fb, 0, 0, 0, 0, true);
    else if (frame == 1) eyesPair(fb, 0, 0, 24, 24, false);
    else                 eyesPair(fb, 0, 0, 28, 28, false);
}

void drawDead(M5Canvas& fb, uint8_t frame) {        // 死：怔住 → 黑 X → 下沉
    if (frame == 0) {
        eyesPair(fb, 0, 0, 0, 0, false);
    } else {
        const int16_t off = (frame == 2) ? 10 : 0;
        drawX(fb, 57, 100 + off, 20, kInk);
        drawX(fb, 183, 100 + off, 20, kInk);
    }
}

void drawDots(M5Canvas& fb, uint8_t frame) {        // 等等：半闭眼 + 琥珀点 1→2→3
    eyesPair(fb, 0, 0, 22, 22, false);
    const uint8_t count = static_cast<uint8_t>(frame + 1);
    const int16_t start = 120 - (count - 1) * 16;
    for (uint8_t i = 0; i < count; ++i) fb.fillCircle(start + i * 32, 150, 7, kAmber);
}

void drawSmile(M5Canvas& fb, uint8_t frame) {       // 笑：柔笑 → 大笑（弧更深）
    if (frame == 0) {
        closedEye(fb, 50, 96, 96, 96, true, kInk, 12);
        closedEye(fb, 144, 96, 190, 96, true, kInk, 12);
    } else {
        closedEye(fb, 48, 94, 98, 94, true, kInk, 22);
        closedEye(fb, 142, 94, 192, 94, true, kInk, 22);
    }
}

void drawLook(M5Canvas& fb, uint8_t frame) {        // 看你：黑眼整体平移（glint 跟随）
    const int16_t dx = (frame == 0) ? -10 : ((frame == 1) ? 0 : 10);
    eyesPair(fb, dx, 0, 0, 0, true);
}

void drawHeartBeat(M5Canvas& fb, uint8_t frame) {   // 心跳：喜悦弯眼 + 玫红心 小→大
    closedEye(fb, 50, 90, 96, 90, true, kInk, 16);
    closedEye(fb, 144, 90, 190, 90, true, kInk, 16);
    drawHeart(fb, 120, 140, frame == 0 ? 34 : 52, kRose);
}

void drawSleep(M5Canvas& fb, uint8_t frame) {       // 睡：半阖眼（复刻 CLAWD idle 打盹）+ 奶白 block z 累加
    eyeRect(fb, kLeftX,  kEyeYT + 2, kEyeW, kEyeH, 40, 0, false);  // 半阖薄缝 + 微下沉，无 glint
    eyeRect(fb, kRightX, kEyeYT + 2, kEyeW, kEyeH, 40, 0, false);
    if (frame >= 1) drawZ(fb, 138, 56, 12, kCream);   // 小 z（近眼）
    if (frame >= 2) drawZ(fb, 158, 32, 18, kCream);   // 大 z（右上飘）
}

void drawAwkward(M5Canvas& fb, uint8_t frame) {     // 尴尬：左右眼不对称 + 青汗滴下滑
    eyeRect(fb, kLeftX,  kEyeYT, kEyeW, kEyeH, 0, 0, true);
    eyeRect(fb, kRightX, kEyeYT, kEyeW, kEyeH, 24, 8, false);
    if (frame >= 1) drawSweat(fb, 204, frame == 1 ? 70 : 92, 30);
}

}  // namespace

const FaceSpec& spec(uint8_t face_index) {
    return kFaces[(face_index < kFaceCount) ? face_index : 0];
}

uint8_t findByKey(const char* key) {
    if (!key) return 0;
    for (uint8_t i = 0; i < kFaceCount; ++i) {
        if (strcmp(kFaces[i].key, key) == 0) return i;
    }
    return 0;
}

const char* keyForIndex(uint8_t face_index) {
    return spec(face_index).key;
}

void drawFace(M5Canvas& fb, uint8_t face_index, uint8_t frame) {
    if (face_index >= kFaceCount) face_index = 0;
    frame = clampFrame(face_index, frame);

    switch (face_index) {
        case 0:  drawDeadpan(fb); break;
        case 1:  drawQuestionFace(fb); break;
        case 2:  drawAlertFace(fb); break;
        case 3:  drawAngry(fb); break;
        case 4:  drawYes(fb); break;
        case 5:  drawNo(fb); break;
        case 6:  drawGlasses(fb); break;
        case 7:  drawWink(fb, frame); break;
        case 8:  drawDizzy(fb, frame); break;
        case 9:  drawClose(fb, frame); break;
        case 10: drawDead(fb, frame); break;
        case 11: drawDots(fb, frame); break;
        case 12: drawSmile(fb, frame); break;
        case 13: drawLook(fb, frame); break;
        case 14: drawHeartBeat(fb, frame); break;
        case 15: drawSleep(fb, frame); break;
        case 16: drawAwkward(fb, frame); break;
        default: drawDeadpan(fb); break;
    }
}

}  // namespace mochi::faces
