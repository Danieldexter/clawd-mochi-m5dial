#pragma once

#include <cstdint>

#include "modes/i_mode.h"
#include "config.h"  // kCcTokenLen（cc_scope 缓冲长度）

namespace mochi::state {

// 背光内部映射：协议层 bool → setBrightness 数值
constexpr uint8_t kBacklightOnLevel  = 200;
constexpr uint8_t kBacklightOffLevel = 0;

// Phase 11：Claude Code 会话状态（联动模式渲染依据；由 /cc 端点的 hook 事件驱动）
enum class CcStatus : uint8_t {
    IDLE    = 0,  // 会话结束 / 待命（红：摇摆眨眼大眼）
    WORKING = 1,  // 思考中（绿：眼上看 + 绿点绕圈）
    WAITING = 2,  // 待确认 / 待权限（黄：瞪大眼 + "!" + 黄环脉冲）
};

// Phase 11：联动模式视觉风格（web 可选 + NVS 持久化）
enum class CcStyle : uint8_t {
    CLAWD   = 0,  // 情绪眼：表情 + 招牌动效
    SPARKLE = 1,  // Claude 星芒核心
};

// Phase 14b：PC Monitor 面板配置（web 可配 + NVS 持久化）。分类索引/掩码位序见 ws_protocol.h kMonCatNames。
struct MonitorCfg {
    bool     rotate       = true;    // true=轮询所有启用分类；false=固定 single_cat
    uint16_t interval_s   = 20;      // 轮询切换间隔（秒）
    uint8_t  single_cat   = 0;       // rotate=false 时固定显示的分类索引（0..5）
    uint8_t  enabled_mask = 0x3F;    // 轮询纳入的分类位掩码（默认 6 类全开）
};

// 跨 mode 共享的协议真值源（single source of truth）。
// WS handler 修改这里 → 调当前 mode 的 applyState → 该 mode 决定立即重绘哪些字段。
struct SharedState {
    ModeId   current_mode      = ModeId::FACE_SHOW;  // v0.3.0：开机 home = 统一表情 mode（anim_idle 摇摆眼）
    uint8_t  speed             = 2;       // 1=fast / 2=normal / 3=slow
    uint16_t bg_color_565      = 0xFA00;  // #ff4000 橙红（默认底色 / 设备身份色）
    uint16_t pen_color_565     = 0x0000;  // #000000 默认（黑色，Phase 7 用）
    bool     backlight         = true;
    char     pc_ip[16]         = {0};     // Phase 9：PC Monitor 目标 IP（NVS 持久化，Phase 13 读用）
    CcStatus cc_status         = CcStatus::IDLE;  // Phase 11：Claude Code 联动状态
    CcStyle  cc_style          = CcStyle::CLAWD;  // Phase 11：联动模式视觉风格（NVS 持久化）
    char     cc_scope[config::kCcTokenLen] = {0}; // Phase 11b：联动作用域——选中项目的 CLAUDE_PROJECT_DIR，空=全局（默认）。NVS 持久化
    uint8_t  face_index        = 0;       // Phase 12：face_show 当前表情索引（Web 可见 API 使用 face_key）
    uint8_t  gif_index         = 0;       // v0.4.0：GIF 图库当前播放槽（0..3；Web 高亮 + 入 mode 初始槽）
    bool     auto_locked       = false;   // v0.3.0：长按锁定——冻结 Faces 自动轮换 & PC Monitor 自动切卡（运行时，不持久/不广播）
    MonitorCfg monitor;                   // Phase 14b：PC Monitor 面板配置（单显/轮询/间隔/启用分类）
    uint32_t last_broadcast_ms = 0;       // 占位，v0.3.0+ 节流用
};

extern SharedState g_state;

}  // namespace mochi::state
