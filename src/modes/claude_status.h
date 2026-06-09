#pragma once

#include "i_mode.h"
#include "../state.h"  // CcStatus / CcStyle 完整定义（成员 + 默认初值需要枚举量）

#include <M5Dial.h>    // M5Canvas（离屏后备缓冲类型）

namespace mochi {

class Canvas;  // 前置声明：onEnter 调 canvas_->releaseSprite() 腾出大缓冲预算（与 Canvas 115KB 互斥，§9）

// Phase 11（抛光）：Claude Code 联动状态模式 —— 双可选风格 × 三态。
// 由 /cc 端点的 hook 事件驱动（写 g_state.cc_status），用表情/动效反映会话状态。
// 风格由 g_state.cc_style 决定（web 选择器 + NVS 持久化）：
//   CLAWD   情绪眼：表情本身传达状态 + 招牌动效（星芒 / 弹跳! / 打盹z）
//   SPARKLE Claude 星芒核心：放射星芒为主角，眼睛退居其次
// 三态语义：
//   IDLE    会话结束 / 待命 —— CLAWD 呼吸+随机眨+打盹 / SPARKLE 小静星+回归慢眨
//   WORKING 思考中          —— CLAWD 下垂专注眼+绿星芒脉冲 / SPARKLE 大星芒慢转微颤
//   WAITING 待确认 / 待权限  —— CLAWD 瞪大眼+弹跳"!"+扫弧 / SPARKLE 冻结琥珀心跳+"!"
// 渲染契约（见 docs/phase11_claude_status_spec.md）：fillScreen 只在 redrawBase；
// tick 每个动效元素"先按 bg_color_ 擦旧再画新"；所有动画极值经验算 ≤ §6 安全圈 r110。
class ClaudeStatus : public IMode {
public:
    ModeId id() const override { return ModeId::CLAUDE_STATUS; }
    void   onEnter() override;
    void   onExit()  override;
    void   tick(uint32_t now_ms) override;
    void   applyState(const state::SharedState& s) override;

    // v0.4.0：main setup 注入 Canvas。onEnter 调 canvas_->releaseSprite() 释放其 115KB——否则轮询
    // 经 Canvas（或 /cc 自动切入）到本 mode 时 28KB createSprite OOM → 黑屏（CLAUDE.md §9）。
    void   attachCanvas(Canvas* c) { canvas_ = c; }

private:
    // ── 静态层（仅状态/风格/背景变化时）──
    void redrawBase();
    // 把 9 个调色板索引映射到各自 RGB565（kBg = 运行时 bg_color_）。
    void applyPalette();

    // ── 共享绘制 helper（spec §2）──
    void    drawEye(int16_t x, int16_t y, int16_t w, int16_t h,
                    int16_t lidTop, int16_t lidBot, bool glint);
    void    drawBang(int16_t cx, int16_t topY, uint16_t col);
    void    drawZ(int16_t x, int16_t y, int16_t s, uint16_t col);
    int16_t drawSparkle(int16_t cx, int16_t cy, const int16_t L[8],
                        int16_t halfW, float rotDeg, int16_t coreR,
                        uint16_t body, uint16_t hi);  // 返回本帧最长射线
    void    eraseSparkle(int16_t cx, int16_t cy, int16_t eraseR);

    // ── per-(style,state) 动效（spec §4/§5）──
    void tickClawdIdle(uint32_t now);
    void tickClawdWorking(uint32_t now);
    void tickClawdWaiting(uint32_t now);
    void tickSparkleWorking(uint32_t now);
    void tickSparkleWaiting(uint32_t now);
    void tickSparkleIdle(uint32_t now);

    // 眨眼公共子机（CLAWD idle/working、SPARKLE idle 复用）。
    // 返回当前 lidTop（0=全开）；done 出参标记本次眨眼是否刚结束。
    int16_t blinkLid(uint32_t now, uint32_t gapMin, uint32_t gapMax,
                     uint32_t durMin, uint32_t durMax);

    // ── 渲染依据（onEnter/applyState 拍照）──
    state::CcStatus shown_status_ = state::CcStatus::IDLE;
    state::CcStyle  shown_style_  = state::CcStyle::CLAWD;
    uint32_t enter_time_ = 0;     // 进入当前态时刻（doze dwell / beat 锚点）
    uint8_t  speed_      = 2;
    uint16_t bg_color_   = 0xFA00;
    uint32_t last_step_  = 0;     // 帧率门控（risk #8：按 elapsed 而非每 tick 一帧）

    // ── 离屏后备缓冲（消除"直绘擦背景→再画"的中间擦除态闪烁）──
    // 渲染契约改为：所有绘制（含按 kBg 擦旧位）先写入 fb_，每帧仅一次 pushSprite
    // 推屏。擦除态只发生在离屏缓冲里，面板永远只收到合成后的成品 → 无闪烁。
    // 用 4bpp 调色板（240×240≈28KB）：本机无 PSRAM，须省内存与 Canvas 的 115KB
    // sprite 共存（spec §1 只用 8 色，16 槽调色板绰绰有余，且保 RGB565 精确显色）。
    M5Canvas fb_{&M5Dial.Display};
    bool     fb_ready_ = false;
    Canvas*  canvas_   = nullptr; // 大缓冲互斥：onEnter 释放其 115KB sprite（§9）
    bool     dirty_    = false;   // 本帧是否有元素改动 → 决定是否 pushSprite

    // ── 眨眼子机 ──
    bool     blinking_     = false;
    uint32_t blink_start_  = 0;
    uint32_t blink_dur_    = 170;
    uint32_t next_blink_ms_= 0;

    // ── CLAWD idle：瞟一眼（唯一水平动，一次性）/ 呼吸 / 打盹 ──
    int8_t   glance_dir_   = 0;    // -1 / +1，0=未瞟
    uint32_t glance_start_ = 0;
    uint32_t next_glance_ms_ = 0;
    bool     dozing_       = false;
    uint32_t z_born_       = 0;    // 当前 z 出生时刻（0=无 z）
    uint32_t next_z_ms_    = 0;
    int16_t  prev_zx_ = -1, prev_zy_ = -1, prev_zs_ = -1;
    int16_t  prev_dy_ = 0x7FFF, prev_lid_ = -1, prev_dx_ = 0x7FFF;  // 眼"变化才重绘"追踪

    // ── sparkle 擦除盘（CLAWD 顶星芒 / SPARKLE hero/star）──
    int16_t  eraseR_ = 0;

    // ── waiting "!" 弹跳追踪 ──
    int16_t  prev_topY_ = -1;

    // ── CLAWD waiting 扫弧 crest ──
    int16_t  sweep_deg_ = 0;
    uint32_t next_crest_ms_ = 0;

    // ── SPARKLE waiting 心跳追踪 ──
    int16_t  prev_beatL_ = -1;
};

}  // namespace mochi
