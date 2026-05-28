#pragma once

#include "modes/i_mode.h"

namespace mochi {

class ModeManager {
public:
    // 注册 mode 实例。不接管 ownership：mode 生命周期由 caller 保证。
    void registerMode(IMode* mode);

    // 切换到指定 mode。未注册时打 log 并忽略。
    void setMode(ModeId id);

    // 主循环驱动当前 mode。
    void tick(uint32_t now_ms);

    ModeId currentId() const { return current_id_; }
    IMode* currentMode() const { return current_; }

private:
    IMode* modes_[4]    = {nullptr, nullptr, nullptr, nullptr};
    IMode* current_     = nullptr;
    ModeId current_id_  = ModeId::NORMAL_EYES;
};

// 全局实例。Phase 6：ws_handler / ap_server 都需要访问 currentMode + setMode。
extern ModeManager g_mochi;

}  // namespace mochi
