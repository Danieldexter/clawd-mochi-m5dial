#pragma once

#include <cstdint>

#include <M5Dial.h>  // M5Canvas（绘制目标类型）

namespace mochi::faces {

// 表情总数（face_index 合法范围 0..kFaceCount-1）。
// 命令 key 沿用 mumuer1024 的 7 静 + 10 动语义；视觉为 M5Dial 圆屏重设。
constexpr uint8_t kFaceCount = 17;

// Web / WS 可见的稳定命令 key 最大长度（含 '\0' 缓冲由 state/config 负责）。
constexpr uint8_t kFaceKeyMaxLen = 20;

// 4bpp 调色板索引约定：调用方（face_show）须按此 setPaletteColor。
constexpr uint8_t kBg      = 0;   // 背景（= 运行时 bg_color_）
constexpr uint8_t kInk     = 1;   // 主描边
constexpr uint8_t kCream   = 2;   // 暖白面部元素
constexpr uint8_t kWhite   = 3;   // 高光
constexpr uint8_t kAmber   = 4;   // 感叹 / 提示
constexpr uint8_t kAmberHi = 5;   // 高亮提示
constexpr uint8_t kGreen   = 6;   // yes / check
constexpr uint8_t kRed     = 7;   // no / dead
constexpr uint8_t kCyan    = 8;   // 汗滴 / 疑问
constexpr uint8_t kGray    = 9;   // dead / dim
constexpr uint8_t kRose    = 10;  // heart / cheek

struct FaceSpec {
    const char* key;          // WS key，如 "face_wuyu" / "anim_smile"
    const char* label_en;     // Web fallback label
    const char* label_zh;     // Web display label
    bool        animated;
    uint8_t     frame_count;  // 1..3
    uint8_t     loop_count;   // 静态=1；循环动画=3；单次动画=1
    bool        hold_final;   // 动画结束后 true=停最后帧，false=回第 0 帧
    uint16_t    frame_ms;     // 非阻塞播放节奏
};

const FaceSpec& spec(uint8_t face_index);
uint8_t         findByKey(const char* key);  // 未找到返回 0
const char*     keyForIndex(uint8_t face_index);

// 把 face_index 对应表情画进 fb_（4bpp 调色板 sprite）。
// frame 由 FaceShow 的非阻塞状态机计算；静态脸忽略 frame。
// 不清屏、不 pushSprite —— 由调用方负责（见 face_show::redraw）。
void drawFace(M5Canvas& fb, uint8_t face_index, uint8_t frame);

}  // namespace mochi::faces
