#pragma once

#include <cstdint>

// 全局跨 mode 共享的常量集。
// 仅放真正跨多个 mode/模块复用的项；单 mode 内的参数留在该 mode 的 .cpp 匿名命名空间。

namespace mochi::config {

// 圆屏几何（240×240，圆心 120,120）。CLAUDE.md §6 要求所有有意义内容落在安全圈内。
constexpr int16_t kScreenCenterX = 120;
constexpr int16_t kScreenCenterY = 120;
constexpr int16_t kSafeRadius    = 110;

// --- WiFi AP（保留原项目 SSID / 密码与用户体验一致）---
constexpr const char* kApSsid     = "ClaWD-Mochi";
constexpr const char* kApPassword = "clawd1234";

// --- Web stack ---
constexpr const char* kWsPath = "/ws";

}  // namespace mochi::config
