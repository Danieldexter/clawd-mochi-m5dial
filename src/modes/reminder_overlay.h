#pragma once

#include "i_mode.h"
#include "../config.h"  // kReminderMsgLen

#include <cstdint>

namespace mochi {

// Phase 13：提醒触发的瞬态全屏消息 mode（直绘，无离屏缓冲；仿 provisioning::drawSetupScreen）。
// loop 编排：reminder fired → show(msg, returnMode, now) → setMode(REMINDER_OVERLAY)；
// 30s 后（或 BtnA 轻点）loop 检测 finished() → setMode(returnMode)。
// 关键：本 mode 永不写入 g_state.current_mode → web 面板仍显示底层 mode（见 main.cpp 编排）。
class ReminderOverlay : public IMode {
public:
    ModeId id() const override { return ModeId::REMINDER_OVERLAY; }
    void   onEnter() override;
    void   onExit()  override;
    void   tick(uint32_t now_ms) override;

    // loop 在 setMode 前调用：存消息 + 返回 mode + 入场时刻。
    void   show(const char* msg, ModeId return_mode, uint32_t now_ms);
    bool   finished(uint32_t now_ms) const;
    ModeId returnMode() const { return return_mode_; }

private:
    void draw(uint32_t now_ms);      // 全屏一次：底色 + "!" + 换行消息 + 倒计时条
    void drawBar(uint32_t now_ms);   // 仅更新底部倒计时条（避免逐帧全屏直绘闪烁）

    char     msg_[config::kReminderMsgLen] = {0};
    ModeId   return_mode_ = ModeId::NORMAL_EYES;
    uint32_t start_ms_    = 0;
    int16_t  last_bar_w_  = -1;   // 倒计时条上次宽度（变化才重绘）
};

}  // namespace mochi
