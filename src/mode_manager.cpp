#include "mode_manager.h"

#include <Arduino.h>

namespace mochi {

ModeManager g_mochi;  // Phase 6：全局实例定义

void ModeManager::registerMode(IMode* mode) {
    if (mode == nullptr) return;
    uint8_t idx = static_cast<uint8_t>(mode->id());
    modes_[idx] = mode;
}

void ModeManager::setMode(ModeId id) {
    uint8_t idx = static_cast<uint8_t>(id);
    if (modes_[idx] == nullptr) {
        Serial.printf("[mode_mgr] mode %u not registered\n", idx);
        return;
    }
    if (current_ != nullptr) {
        current_->onExit();
    }
    current_    = modes_[idx];
    current_id_ = id;
    current_->onEnter();
}

void ModeManager::tick(uint32_t now_ms) {
    if (current_ != nullptr) {
        current_->tick(now_ms);
    }
}

// v0.3.0：旋钮单击在 FACE_SHOW(0)..GIF_PLAYER(5) 6 个 mode 间轮询，跳过 REMINDER_OVERLAY(6)。
ModeId ModeManager::nextInCycle() const {
    constexpr uint8_t kCycleCount = 6;  // FACE_SHOW..GIF_PLAYER
    const uint8_t next = (static_cast<uint8_t>(current_id_) + 1) % kCycleCount;
    return static_cast<ModeId>(next);
}

}  // namespace mochi
