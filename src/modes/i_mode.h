#pragma once

#include <cstdint>

namespace mochi {

enum class ModeId : uint8_t {
    NORMAL_EYES   = 0,
    SQUISH_EYES   = 1,
    CLAUDE_CODE   = 2,
    CANVAS        = 3,
    CLAUDE_STATUS = 4,  // Phase 11：Claude Code 联动状态模式（思考/待确认/待命）
    FACE_SHOW     = 5,  // Phase 12：表情系统展示 mode（按 face key 渲染 17 表情）
    PC_MONITOR    = 6,  // Phase 14：PC 监控面板（拉 PC stats.json 圆形显示）
    REMINDER_OVERLAY = 7,  // Phase 13：提醒触发的瞬态全屏消息（不进 g_state.current_mode）
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
