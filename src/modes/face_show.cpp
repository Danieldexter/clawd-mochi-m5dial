#include "face_show.h"

#include "canvas.h"          // releaseSprite()：onEnter 释放 Canvas 115KB（大缓冲互斥，§9）
#include "../state.h"
#include "../faces/faces_data.h"

#include <M5Dial.h>

namespace mochi {

namespace {

// RGB565 → fb_ 调色板索引（5/6/5 → 8/8/8 复制扩展，无歧义）。
// 同 claude_status.cpp:104 的 setPal565（4 行，2 处用不抽公共头）。
inline void setPal565(M5Canvas& fb, uint8_t idx, uint16_t c) {
    const uint8_t r5 = (c >> 11) & 0x1F, g6 = (c >> 5) & 0x3F, b5 = c & 0x1F;
    fb.setPaletteColor(idx, static_cast<uint8_t>((r5 << 3) | (r5 >> 2)),
                            static_cast<uint8_t>((g6 << 2) | (g6 >> 4)),
                            static_cast<uint8_t>((b5 << 3) | (b5 >> 2)));
}

constexpr uint32_t kReactMs    = 1000;  // v0.3.0：tap 反应脸（wink）显示时长
constexpr uint32_t kIdleStepMs = 50;    // v0.3.0：anim_idle 连续重绘步进（摇摆平滑且不过刷）

}  // namespace

void FaceShow::applyPalette() {
    setPal565(fb_, faces::kBg,      bg_color_);  // 背景：运行时可变
    setPal565(fb_, faces::kInk,     0x0000);
    setPal565(fb_, faces::kCream,   0xFF38);
    setPal565(fb_, faces::kWhite,   0xFFFF);
    setPal565(fb_, faces::kAmber,   0xFC60);
    setPal565(fb_, faces::kAmberHi, 0xFFE0);
    setPal565(fb_, faces::kGreen,   0x07E0);
    setPal565(fb_, faces::kRed,     0xF800);
    setPal565(fb_, faces::kCyan,    0x07FF);
    setPal565(fb_, faces::kGray,    0x8410);
    setPal565(fb_, faces::kRose,    0xF81F);
}

void FaceShow::startFace(uint8_t face_index, uint32_t now_ms) {
    shown_face_index_ = (face_index < faces::kFaceCount) ? face_index : 0;
    shown_frame_      = 0;
    anim_start_ms_    = now_ms;
}

uint8_t FaceShow::frameFor(uint32_t now_ms) const {
    const auto& spec = faces::spec(shown_face_index_);
    if (!spec.animated || spec.frame_count <= 1) return 0;

    const uint32_t elapsed = now_ms - anim_start_ms_;
    const uint32_t step    = elapsed / spec.frame_ms;
    const uint32_t total   = static_cast<uint32_t>(spec.frame_count) * spec.loop_count;
    if (step >= total) return spec.hold_final ? static_cast<uint8_t>(spec.frame_count - 1) : 0;
    return static_cast<uint8_t>(step % spec.frame_count);
}

void FaceShow::redraw(uint8_t frame) {
    if (!fb_ready_) return;
    fb_.fillSprite(faces::kBg);
    if (shown_face_index_ == faces::kFaceIdle)
        faces::drawIdleEyes(fb_, millis(), anim_start_ms_);  // 摇摆眼：连续，按时间取相位
    else
        faces::drawFace(fb_, shown_face_index_, frame);
    fb_.pushSprite(0, 0);
}

void FaceShow::onEnter() {
    if (canvas_) canvas_->releaseSprite();  // 释放 Canvas 115KB（否则 web 直跳 Canvas→Faces 时本 mode 28KB OOM → 黑屏，§9）
    bg_color_ = state::g_state.bg_color_565;
    startFace(state::g_state.face_index, millis());

    if (!fb_ready_) {
        fb_.setPsram(false);          // 无 PSRAM，显式落内部 SRAM（28KB）
        fb_.setColorDepth(4);         // palette_4bit → createSprite 自动建 16 槽调色板
        fb_ready_ = (fb_.createSprite(240, 240) != nullptr);
        if (fb_ready_) {
            applyPalette();
            // onEnter 已释放 Canvas 115KB（§9 单大缓冲），此 freeheap 反映"仅本 mode 28KB + WiFi"余量。
            Serial.printf("[face_show] fb 4bpp ok, freeheap=%u\n", ESP.getFreeHeap());
        } else {
            Serial.println("[face_show] fb createSprite FAILED -> direct fallback");
        }
    } else {
        applyPalette();               // 离开期间 bg 可能改了，重设调色板（含 kBg）
    }

    if (fb_ready_) redraw(shown_frame_);
    else           M5Dial.Display.fillScreen(bg_color_);   // 退化：静态底色
}

void FaceShow::onExit() {
    // v0.3.0：退出即释放 28KB（轮询轮到 Canvas 115KB 时避免共存 OOM）；onEnter 惰性重建。
    if (fb_ready_) { fb_.deleteSprite(); fb_ready_ = false; }
}

void FaceShow::tick(uint32_t now_ms) {
    if (!fb_ready_) return;

    // tap 反应到点 → 回到当前真值脸（g_state.face_index）。
    if (react_until_ms_ != 0 && now_ms >= react_until_ms_) {
        react_until_ms_ = 0;
        startFace(state::g_state.face_index, now_ms);
        redraw(shown_frame_);
        return;
    }

    // anim_idle：连续摇摆+眨眼，按帧率门控每帧重绘（与离散帧路径脱钩）。
    if (shown_face_index_ == faces::kFaceIdle) {
        if (now_ms - last_idle_ms_ >= kIdleStepMs) {
            last_idle_ms_ = now_ms;
            redraw(0);  // idle 忽略 frame，redraw 内按 millis 取相位
        }
        return;
    }

    const uint8_t frame = frameFor(now_ms);
    if (frame != shown_frame_) {
        shown_frame_ = frame;
        redraw(shown_frame_);
    }
}

// 触摸 tap：临时眨一下（wink）~1s 后自动回当前真值脸。仅改 shown_，不动 g_state.face_index
// （web 仍显真值脸；若反应期间编码器/轮换改了 g_state，回切即落新值）。
void FaceShow::react(uint32_t now_ms) {
    if (!fb_ready_) return;
    startFace(faces::findByKey("anim_jiyanjing"), now_ms);
    react_until_ms_ = now_ms + kReactMs;
    redraw(0);
}

// 自动轮换：强制从头重播 g_state.face_index（即使抽中同一张也重播；取消进行中的 tap 反应）。
void FaceShow::restart(uint32_t now_ms) {
    if (!fb_ready_) return;
    react_until_ms_ = 0;
    startFace(state::g_state.face_index, now_ms);
    redraw(0);
}

void FaceShow::applyState(const state::SharedState& s) {
    const bool bg_changed   = (s.bg_color_565 != bg_color_);
    const bool face_changed = (s.face_index != shown_face_index_);
    bg_color_ = s.bg_color_565;
    if (face_changed) startFace(s.face_index, millis());

    if (!fb_ready_) {
        if (bg_changed) M5Dial.Display.fillScreen(bg_color_);   // 退化路径
        return;
    }
    if (bg_changed) setPal565(fb_, faces::kBg, bg_color_);      // 调色板 kBg → 新底色
    if (bg_changed || face_changed) redraw(shown_frame_);
}

}  // namespace mochi
