#pragma once

#include <cstdint>

// 全局跨 mode 共享的常量集。
// 仅放真正跨多个 mode/模块复用的项；单 mode 内的参数留在该 mode 的 .cpp 匿名命名空间。

namespace mochi::config {

// 圆屏几何（240×240，圆心 120,120）。CLAUDE.md §6 要求所有有意义内容落在安全圈内。
constexpr int16_t kScreenCenterX = 120;
constexpr int16_t kScreenCenterY = 120;
constexpr int16_t kSafeRadius    = 110;

// --- WiFi 配网（Phase 9）---
// v0.2.0 起：正常运行 = STA（设备加入用户 WiFi）；首启/无凭据/连接失败 = 开放配网 AP。
// v0.1.0 的常驻控制 AP（ClaWD-Mochi/clawd1234）已取消，详见 CLAUDE.md §9。
constexpr const char* kSetupApSsid       = "Clawd-Mochi-Setup";  // 配网开放热点，无密码
constexpr const char* kMdnsHost          = "clawd-mochi";        // → http://clawd-mochi.local
constexpr const char* kNvsNamespace      = "clawd";              // Preferences namespace（≤15 字符）
constexpr uint32_t    kStaConnectTimeoutMs = 15000;              // STA 连接超时（ms）

// --- Web stack ---
constexpr const char* kWsPath = "/ws";

// --- Claude Code 联动作用域（Phase 11b）---
// hook 经 /cc?...&p= 传 $CLAUDE_PROJECT_DIR；设备据此过滤要反映哪个项目（cc_scope 空=全局）。
constexpr int kCcTokenLen    = 128;  // 项目标识缓冲长度（超长截断；存储与比较保持一致）
constexpr int kCcProjectsMax = 8;    // Web 下拉记忆的项目数上限（newest-first）

// --- Reminder 定时提醒（Phase 13）---
constexpr int      kRemindersMax   = 5;        // 最多提醒条数（mumuer1024 同款上限）
constexpr int      kReminderMsgLen = 96;       // 单条消息缓冲（含 null；前端限 20 字符，中英 emoji 混排无截断）
constexpr uint32_t kReminderShowMs = 30000;    // 触发后全屏显示时长，到点回原 mode（30s）
// NTP：提醒按墙钟时间 HH:MM 触发。时区固定 UTC+8（用户在中国），将来要改集中此处。
constexpr long        kTzOffsetSec = 8L * 3600;
constexpr const char* kNtpServer1  = "pool.ntp.org";
constexpr const char* kNtpServer2  = "ntp.aliyun.com";

}  // namespace mochi::config
