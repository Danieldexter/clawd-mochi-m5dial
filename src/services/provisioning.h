#pragma once

#include <cstdint>

namespace mochi::state { struct MonitorCfg; }  // 前置声明，避免头里 include state.h

// WiFi 配网 + NVS 凭据生命周期的单一拥有者（Phase 9/10）。
// 正常运行 = STA（设备加入用户 WiFi）；首启/无凭据/连接失败 = 开放配网 AP + captive portal。
// 详见 CLAUDE.md §9 与 docs/workflow.md Phase 9/10。
namespace mochi::provisioning {

// 读 NVS 凭据到模块缓存 + 同步 state::g_state.pc_ip。返回 true=已存 ssid（有凭据）。
bool begin();

// STA 连接（阻塞，仅在 setup() 调用，非主循环）。返回是否在 timeout 内连上。
bool connectSTA(uint32_t timeout_ms);

// 启动开放配网 AP（WIFI_AP_STA，STA 空闲供 /scan）+ DNS captive portal。置 setup_mode。
void startSetupAP();

// STA 连上后广播 mDNS（http://<kMdnsHost>.local）。失败仅 log。
void startMdns();

// 配网模式主循环调：处理 DNS 请求（轻量）。
void tickDNS();

// 写三键凭据到 NVS（配网页 /save 用）。
bool saveConfig(const char* ssid, const char* pass, const char* pcip);

// 仅写 pcip + 更新 state（Settings 页 set_pc_ip 用）。
void savePcIp(const char* ip);

// 写联动模式视觉风格到 NVS（Settings 页 set_cc_style 用）+ 更新 state。
void saveCcStyle(uint8_t style);

// 写联动作用域过滤到 NVS（Settings 页 set_cc_scope 用）+ 更新 state。空串=全局。
void saveCcScope(const char* scope);

// 写 PC Monitor 面板配置到 NVS（set_monitor 用）+ 更新 state::g_state.monitor。begin() 启动时回读。
void saveMonitorCfg(const state::MonitorCfg& cfg);

// 记录最近触发 /cc?p= 的 Claude Code 项目。最多 config::kCcProjectsMax 个，newest-first。
void registerCcProject(const char* project);
uint8_t ccProjectCount();
const char* ccProjectAt(uint8_t index);

// 清空 NVS namespace（wifi_reset 用）。
void clearConfig();

// 按需配网：置一次性标志，请求下次启动进配网二维码屏（BtnA 长按 / Settings Reset WiFi）。
// 不动凭据——默认开机进设备界面（动画），仅此标志或显式请求才进配网（详见 CLAUDE.md §9）。
void requestSetupMode();
// setup() 启动时调一次：读并清标志。返回 true=本次启动应进配网屏。
bool consumeSetupRequest();

// 延迟重启：置标志 + 截止时间。禁止在 AsyncTCP/WS 回调里直接 ESP.restart()（CLAUDE.md §1.6）。
void requestRestart(uint32_t delay_ms = 600);
// 主循环调：到点执行 ESP.restart()。
void tickRestart(uint32_t now_ms);

bool        isSetupMode();
const char* pcIp();
const char* ssid();

// 配网屏：居中 WiFi 加入二维码 + 极简标签（§6 安全圈内）。
void drawSetupScreen();

}  // namespace mochi::provisioning
