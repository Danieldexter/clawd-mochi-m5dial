#pragma once

#include "i_mode.h"

#include <M5Dial.h>  // M5Canvas（离屏后备缓冲类型）

namespace mochi {

// Phase 14b：PC Monitor 模式 —— 6 分类圆屏卡片（CPU / Disk / GPU / Host / Network / Traffic）。
// 默认轮询切换（web 可配单显/轮询/间隔/启用分类），深色基底 + 每类强调色 + 弧形仪表/进度条/数值混排。
// 出站 HTTP 走常驻 FreeRTOS task（阻塞 GET 不卡 loop，CLAUDE.md §1.6）；渲染 4bpp 离屏缓冲（仿 face_show，无 PSRAM 用调色板省 SRAM）。
// stats.json v2 契约见 pc_monitor/pc_monitor.py：嵌套子对象 cpu/gpu/host/disk/net/traf + av 可用性 map + clk 时钟（取自 PC，无需 NTP）。
class PcMonitor : public IMode {
public:
    ModeId id() const override { return ModeId::PC_MONITOR; }
    void   onEnter() override;
    void   onExit()  override;
    void   tick(uint32_t now_ms) override;
    void   applyState(const state::SharedState& s) override;

    // v0.3.0：编码器在本 mode 步进当前分类卡（enabled∧available 间双向移动 + 暂停自动轮询一拍）。
    void   nudgeCard(int delta);

private:
    void applyPalette();      // 固定槽（kBg/kInk/kWhite/kDim/kTrack）；kAccent 每卡在 redraw 重涂
    void redraw();            // 拷贝 snapshot → fillSprite + chrome + 当前卡 → pushSprite 一次
    void syncCfg();           // g_state.monitor → 成员缓存
    void pickInitialCard();   // 按 cfg 选初始 active（单显=single，轮询=首个启用∧可用）
    void advanceCard();       // 轮询：跳到下一个启用∧可用分类

    M5Canvas fb_{&M5Dial.Display};
    bool     fb_ready_       = false;
    // 面板配置缓存（onEnter/applyState 同步自 state::g_state.monitor）
    bool     rotate_         = true;
    uint32_t interval_ms_    = 20000;
    uint8_t  single_cat_     = 0;
    uint8_t  enabled_mask_   = 0x3F;
    uint8_t  active_cat_     = 0;       // 当前显示的分类索引（0..5，序见 ws_protocol kMonCatNames）
    uint32_t last_switch_ms_ = 0;      // 上次轮询切换时刻
    uint32_t last_seq_       = 0;      // 上次已渲染的 snapshot 序号
};

// buildStateJson 用：当前 PC 上报了哪些分类（bit i = 分类 i 有数据，序同 ws_protocol kMonCatNames）。
namespace pcmon { uint8_t availableMask(); }

}  // namespace mochi
