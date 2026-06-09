#pragma once

#include "i_mode.h"

#include <M5Dial.h>  // M5Canvas（GIF 解码后备缓冲）

// 前置声明（全局命名空间）：AnimatedGIF 实例为指针成员，onEnter new / onExit delete，
// 避免把 ~24KB 解码结构体静态常驻（只在本 mode 期间占内存）。
class AnimatedGIF;

namespace mochi {

class Canvas;  // 前置声明：onEnter 调 canvas_->releaseSprite() 腾出大缓冲预算

// v0.4.0：GIF 播放器 mode —— Web 上传的 GIF 在设备端用 bitbank2 AnimatedGIF 解码、循环播放。
// 设备无 PSRAM（CLAUDE.md §9）：进入时释放 Canvas 的 115KB sprite（二者互斥共享"大缓冲预算"），
// 解码缓冲 = GIF 原生尺寸 16bpp sprite（源限 ≤240px），播放帧缩放 cover 铺满圆屏（圆形边框天然裁角）。
// 同时只驻留当前 index 的 1 张；AnimatedGIF 实例 + sprite 随 onEnter/onExit 一起 new/delete。
// 解码用 RAW 模式 + 自管持久 sprite 合成（透明像素保留上一帧，常见 disposal 正确，单一缓冲）。
class GifPlayer : public IMode {
public:
    ModeId id() const override { return ModeId::GIF_PLAYER; }
    void   onEnter() override;
    void   onExit()  override;
    void   tick(uint32_t now_ms) override;
    void   applyState(const state::SharedState& s) override;

    // main setup 注入 Canvas：onEnter 调 canvas_->releaseSprite() 让两大缓冲不共存。
    void attachCanvas(Canvas* c) { canvas_ = c; }

    // 编码器旋转：在图库已上传的多张间切换（close/free 当前 → open/alloc 下一张）。
    void nudgeIndex(int delta);

    // P3：Web 图库命令（在 loop 上下文执行，避开 AsyncTCP 的 FS/解码/绘屏竞争）。
    void selectIndex(int idx);  // 切到指定槽（idx<0 = 保持当前，仅夹紧/重载）
    void deleteSlot(int idx);   // 删除槽：先关解码句柄再删（防 rename-over-open）+ 重载

private:
    bool openCurrent();               // 按 index_ 打开 GIF + 建原生 sprite；失败 → playable_=false
    void closeCurrent();              // close gif + free sprite
    void pushCover();                 // fb_ 缩放 cover 推到屏幕居中
    void drawError(const char* msg);  // 无图 / 解码失败：小字形 + 蜂鸣

    M5Canvas     fb_{&M5Dial.Display};  // GIF 原生尺寸 16bpp 解码缓冲（onEnter 建 / onExit 删）
    bool         fb_ready_ = false;
    AnimatedGIF* gif_      = nullptr;   // 指向文件内静态解码器（open 时绑定 / close 时置空；非 heap new，免碎片）
    Canvas*      canvas_   = nullptr;   // 大缓冲互斥：onEnter 释放其 sprite

    int      index_      = 0;       // 当前图库槽（/gifs/<index_>.gif）
    int      gw_ = 0, gh_ = 0;      // GIF 原生画布尺寸（cover 缩放 + pivot 用）
    bool     playable_   = false;   // gif 打开 + sprite 就绪
    uint8_t  speed_      = 2;       // 帧率缩放（1 快 / 2 正常 / 3 慢，取自 g_state.speed）
    char     path_[24]   = {0};     // 当前槽路径缓冲

    uint32_t last_frame_ms_  = 0;
    uint32_t frame_delay_ms_ = 0;   // 当前帧应停留时长（按 speed 缩放）
    uint8_t  prev_disposal_  = 0;   // 上一帧 disposal method（决定下一帧前是否清背景，防透明残影）
    int      prev_x_ = 0, prev_y_ = 0, prev_w_ = 0, prev_h_ = 0;  // 上一帧矩形：disposal!=1 时只擦此矩形（非整屏，防露黑条）
};

}  // namespace mochi
