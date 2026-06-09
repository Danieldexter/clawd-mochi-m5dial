#include "gif_player.h"

#include "canvas.h"
#include "../state.h"
#include "../audio/beeper.h"
#include "../services/gif_store.h"

#include <M5Dial.h>
#include <LittleFS.h>
#include <AnimatedGIF.h>
#include <cstdio>

namespace mochi {

namespace {

// 颜色字节序：M5GFX sprite 的 pushImage 默认按 swap565（大端）解释裸 uint16_t（_swapBytes=false，
// 见 LGFXBase.hpp:1194 create_pc），故 AnimatedGIF 必须输出**大端**调色板（_BE）才匹配。否则彩色像素
// 字节交换：黑(0x0000)/白(0xFFFF)字节序不变（主体看着正常），但彩色背景被交换显示成绿色——
// 这正是"GIF 四周绿边"的根因（v0.4.0 修复，原误用 _LE）。
constexpr uint8_t kPaletteType    = GIF_PALETTE_RGB565_BE;
constexpr int     kDefaultFrameMs = 100;   // GIF 未给帧延时时的默认值
constexpr int     kMinFrameMs     = 20;    // 帧延时下限，防 0ms 狂转占满 loop

// 单张解码，故全局共用一个 File 句柄 + 行缓冲（iWidth ≤ 画布宽 ≤ 240）。
File        g_file;
uint16_t    g_line[240];
AnimatedGIF g_decoder;            // 静态解码器单例（~24KB 常驻 .bss）：避免反复 heap new/delete 造成堆碎片
uint8_t     g_frame_disposal = 0; // 回调记录当前帧 disposal method（tick 据此决定下一帧前是否清背景）
int         g_frame_x = 0, g_frame_y = 0, g_frame_w = 0, g_frame_h = 0;  // 回调记录当前帧矩形（局部擦背景用）

// ── AnimatedGIF 文件回调（LittleFS 流式读，不整文件入 RAM）──
void* gifOpenCB(const char* fname, int32_t* pSize) {
    g_file = LittleFS.open(fname, "r");
    if (!g_file) return nullptr;
    *pSize = static_cast<int32_t>(g_file.size());
    return static_cast<void*>(&g_file);
}
void gifCloseCB(void* handle) {
    File* f = static_cast<File*>(handle);
    if (f && *f) f->close();
}
int32_t gifReadCB(GIFFILE* pf, uint8_t* buf, int32_t len) {
    File* f = static_cast<File*>(pf->fHandle);
    int32_t avail = pf->iSize - pf->iPos;
    if (len > avail) len = avail;
    if (len <= 0) return 0;
    int32_t r = f->read(buf, len);
    pf->iPos = f->position();
    return r;
}
int32_t gifSeekCB(GIFFILE* pf, int32_t pos) {
    File* f = static_cast<File*>(pf->fHandle);
    f->seek(pos);
    pf->iPos = pos;
    return pos;
}

// ── GIFDRAW 回调（RAW 模式）：把一行索引像素经调色板合成进 pUser 指向的 sprite。
// 透明像素跳过（保留上一帧 → 常见 disposal 正确）；不透明连续段整段 pushImage（少事务，§6 性能）。
void gifDrawCB(GIFDRAW* pDraw) {
    M5Canvas* fb = static_cast<M5Canvas*>(pDraw->pUser);
    if (!fb) return;
    g_frame_disposal = pDraw->ucDisposalMethod;     // 记录本帧 disposal（tick 据此决定下一帧清屏）
    g_frame_x = pDraw->iX; g_frame_y = pDraw->iY;    // 记录本帧矩形（下一帧按此**局部**擦背景，不动静止边缘）
    g_frame_w = pDraw->iWidth; g_frame_h = pDraw->iHeight;
    const int       y   = pDraw->iY + pDraw->y;
    int             w   = pDraw->iWidth;
    if (w > 240) w = 240;                            // 防护：行缓冲 g_line[240] 绝不越界
    const uint8_t*  s   = pDraw->pPixels;
    const uint16_t* pal = pDraw->pPalette;
    if (pDraw->ucHasTransparency) {
        const uint8_t tcol = pDraw->ucTransparent;
        int x = 0;
        while (x < w) {
            while (x < w && s[x] == tcol) ++x;          // 跳过透明段（留上一帧像素）
            const int start = x;
            int n = 0;
            while (x < w && s[x] != tcol) { g_line[n++] = pal[s[x]]; ++x; }
            if (n) fb->pushImage(pDraw->iX + start, y, n, 1, g_line);
        }
    } else {
        for (int x = 0; x < w; ++x) g_line[x] = pal[s[x]];
        fb->pushImage(pDraw->iX, y, w, 1, g_line);
    }
}

}  // namespace

void GifPlayer::closeCurrent() {
    if (gif_) { gif_->close(); gif_ = nullptr; }      // 静态单例：只 close（关文件句柄），不 delete
    if (fb_ready_) { fb_.deleteSprite(); fb_ready_ = false; }
    playable_ = false;
}

bool GifPlayer::openCurrent() {
    closeCurrent();                                  // 切换前清干净（close 旧 file + free 旧 sprite）
    const int n = gif_store::count();
    if (n <= 0) { drawError("No GIF"); return false; }
    index_ = ((index_ % n) + n) % n;                 // 归一化到 [0,n)
    gif_store::pathFor(index_, path_, sizeof(path_));
    if (!gif_store::exists(index_)) { drawError("No GIF"); return false; }

    gif_ = &g_decoder;                                // 静态解码器单例（不 heap new，省反复 24KB 分配 + 碎片）
    gif_->begin(kPaletteType);
    if (!gif_->open(path_, gifOpenCB, gifCloseCB, gifReadCB, gifSeekCB, gifDrawCB)) {
        Serial.printf("[gif] open %s failed err=%d\n", path_, gif_->getLastError());
        drawError("Bad GIF");
        gif_->close(); gif_ = nullptr;
        return false;
    }
    gw_ = gif_->getCanvasWidth();
    gh_ = gif_->getCanvasHeight();
    if (gw_ <= 0 || gh_ <= 0 || gw_ > 240 || gh_ > 240) {   // 源限 ≤240px：超则拒，绝不 OOM 试探
        Serial.printf("[gif] canvas %dx%d unsupported (>240)\n", gw_, gh_);
        drawError("max 240px");
        gif_->close(); gif_ = nullptr;
        return false;
    }

    fb_.setPsram(false);                              // 无 PSRAM，显式落内部 SRAM
    fb_.setColorDepth(16);                            // true color（GIF 至多 256 色，逐帧调色板）
    fb_ready_ = (fb_.createSprite(gw_, gh_) != nullptr);
    if (!fb_ready_) {
        Serial.printf("[gif] createSprite %dx%d FAILED freeheap=%u\n",
                      gw_, gh_, static_cast<unsigned>(ESP.getFreeHeap()));
        drawError("OOM");
        gif_->close(); gif_ = nullptr;
        return false;
    }
    fb_.setPivot(gw_ / 2.0f, gh_ / 2.0f);             // pushRotateZoom 以画面中心为轴
    fb_.fillSprite(TFT_BLACK);                        // 首帧前底色（透明区落黑）
    Serial.printf("[gif] open %s %dx%d ok freeheap=%u\n",
                  path_, gw_, gh_, static_cast<unsigned>(ESP.getFreeHeap()));
    M5Dial.Display.fillScreen(TFT_BLACK);
    state::g_state.gif_index = static_cast<uint8_t>(index_);  // 写回（夹紧后的真值，供 Web 高亮）
    last_frame_ms_  = 0;                              // tick 立即解第一帧
    frame_delay_ms_ = 0;
    prev_disposal_  = 0;                              // 新开：首帧按"替换"处理（fb_ 已填黑）
    prev_x_ = prev_y_ = prev_w_ = prev_h_ = 0;        // 首帧前"上一帧矩形"为空 → 首次局部擦 = no-op（fb_ 已整屏黑）
    playable_       = true;
    return true;
}

void GifPlayer::pushCover() {
    if (!fb_ready_) return;
    const float zx = 240.0f / gw_;
    const float zy = 240.0f / gh_;
    const float zoom = (zx > zy) ? zx : zy;           // cover：取大者铺满，溢出由圆形边框裁掉
    fb_.pushRotateZoom(120.0f, 120.0f, 0.0f, zoom, zoom);  // 无 AA 变体 = 最近邻，像素清晰
}

void GifPlayer::drawError(const char* msg) {
    playable_ = false;
    auto& d = M5Dial.Display;
    d.fillScreen(TFT_BLACK);
    d.setTextDatum(middle_center);
    d.setTextColor(TFT_WHITE);
    d.setFont(&fonts::FreeSans12pt7b);
    d.drawString(msg, 120, 108);
    d.setTextColor(0xBDF7);                            // 浅灰副提示
    d.setFont(&fonts::FreeSans9pt7b);
    d.drawString("upload via web", 120, 138);
    audio::beeper::tick();                             // 短鸣提示
}

void GifPlayer::onEnter() {
    if (canvas_) canvas_->releaseSprite();             // 腾出 115KB 大缓冲预算（互斥，§9）
    speed_ = state::g_state.speed;
    index_ = state::g_state.gif_index;                 // 入 mode 沿用 Web / 上次选择的槽
    openCurrent();                                     // 成功→tick 播放；失败→已绘 error
}

void GifPlayer::onExit() {
    closeCurrent();                                    // 释放 sprite + AnimatedGIF（Canvas 下次懒重建）
}

void GifPlayer::tick(uint32_t now_ms) {
    if (!playable_ || !gif_ || !fb_ready_) return;
    if (now_ms - last_frame_ms_ < frame_delay_ms_) return;   // §1.6：按帧延时自管节奏，不阻塞

    // 上一帧 disposal != 1(do-not-dispose) → 只把**上一帧占用的矩形**恢复成背景（非整屏 fillSprite）。
    // 整屏清会抹黑不在任何帧矩形内的静止区（优化型 GIF：首帧铺满、后续仅中部小矩形更新）——循环回首帧那
    // 一刻露出黑底 = 用户所见"上下黑长条闪一下"。按 GIF spec 只擦上一帧矩形：移动主体残影照样消除，静止
    // 边缘（首帧内容）保留 → 既不残影也不闪黑条。整帧型 GIF 的矩形=整屏，等价旧行为，无回归。
    if (prev_disposal_ != 1) fb_.fillRect(prev_x_, prev_y_, prev_w_, prev_h_, TFT_BLACK);
    int delay_ms = 0;
    const int more = gif_->playFrame(false, &delay_ms, &fb_);  // 解一帧 → 回调合成进 fb_ + 记 disposal/矩形
    prev_disposal_ = g_frame_disposal;                         // 本帧 disposal（下一帧据此决定是否局部擦）
    prev_x_ = g_frame_x; prev_y_ = g_frame_y;                  // 本帧矩形（下一帧局部擦的范围）
    prev_w_ = g_frame_w; prev_h_ = g_frame_h;
    pushCover();                                               // 一次性缩放推屏（无逐笔刷屏）
    last_frame_ms_ = now_ms;

    int d = (delay_ms <= 0) ? kDefaultFrameMs : delay_ms;
    if      (speed_ == 1) d /= 2;                              // 快
    else if (speed_ == 3) d *= 2;                              // 慢
    if (d < kMinFrameMs) d = kMinFrameMs;
    frame_delay_ms_ = static_cast<uint32_t>(d);

    if (!more) gif_->reset();                                  // 末帧后回到第一帧循环
}

void GifPlayer::applyState(const state::SharedState& s) {
    speed_ = s.speed;   // 帧率缩放即时生效（下一帧用新 delay）；cover 铺满故不跟随 bg_color
}

void GifPlayer::nudgeIndex(int delta) {
    const int n = gif_store::count();
    if (delta == 0 || n <= 1) return;     // 0/1 张无可切
    index_ = ((index_ + delta) % n + n) % n;
    openCurrent();                         // close 旧 + open 新（失败时 drawError）
}

void GifPlayer::selectIndex(int idx) {
    if (idx >= 0) index_ = idx;
    openCurrent();                         // 内部 closeCurrent + 夹紧 index_ + 写回 gif_index
}

void GifPlayer::deleteSlot(int idx) {
    closeCurrent();                        // 先关解码 file/sprite，避免删/rename 正打开的文件
    if (idx < index_) --index_;            // 删的在前 → 前移以仍指向同一张
    gif_store::removeAt(idx);
    openCurrent();                         // 重开（夹紧到新 count；空则 drawError "No GIF"）
}

}  // namespace mochi
