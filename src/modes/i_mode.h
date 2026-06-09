#pragma once

#include <cstdint>

namespace mochi {

// v0.3.0：Normal/Squish Eyes 已并入 FACE_SHOW（含 anim_idle 摇摆眼 home）。
// 0..5 为旋钮单击轮询的 6 个 mode，顺序即轮询顺序；REMINDER_OVERLAY 为瞬态 overlay，永不入轮询。
enum class ModeId : uint8_t {
    FACE_SHOW        = 0,  // 统一表情 mode（17 表情 + anim_idle 摇摆眼，30s 自动轮换 / 旋钮浏览）
    CLAUDE_CODE      = 1,  // 终端文字滚动
    CANVAS           = 2,  // 触摸 / Web 绘画
    CLAUDE_STATUS    = 3,  // Phase 11：Claude Code 联动状态（思考/待确认/待命）
    PC_MONITOR       = 4,  // Phase 14：PC 监控面板
    GIF_PLAYER       = 5,  // v0.4.0：Web 上传 GIF 循环播放（设备端 AnimatedGIF 解码，≤4 张图库）
    REMINDER_OVERLAY = 6,  // Phase 13：提醒触发的瞬态全屏消息（不进 g_state.current_mode / 不进轮询）
};

}  // namespace mochi

// 前置声明，避免 i_mode.h ↔ state.h 循环 include
namespace mochi::state { struct SharedState; }

namespace mochi {

class IMode {
public:
    virtual ~IMode() = default;
    virtual ModeId id() const = 0;
    virtual void   onEnter()  = 0;
    virtual void   onExit()   = 0;
    virtual void   tick(uint32_t now_ms) = 0;
    // Phase 6：协议变更触发的状态同步。默认空（Phase 5 mode 不需要改）
    virtual void   applyState(const state::SharedState& s) { (void)s; }
};

}  // namespace mochi
