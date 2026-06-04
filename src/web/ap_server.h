#pragma once

#include <Arduino.h>

// 前置声明：consumeCcEvent 用 CcStatus 引用（固定底层类型枚举，前置声明足够）
namespace mochi::state { enum class CcStatus : uint8_t; }

namespace mochi::web::WebStack {

// 启动 LittleFS + AsyncWebServer。WiFi 由 provisioning 在调用本函数前拉起（§9）。
// setup_mode=true：配网模式 — 伺服 setup.html + captive portal + /scan + /save，不挂 WS。
// setup_mode=false：正常模式 — 伺服 index.html/settings.html + WS 控制管道。
// 失败时降级（log + 跳过对应模块），不阻塞 mode_manager 主循环。CLAUDE.md §1.6。
void begin(bool setup_mode);

// 在主循环里定期调用。当前仅 1 Hz 跑 `ws.cleanupClients()`。
// 必须在 loop 线程调（不要在 onWsEvent 回调里调，那个跑在 AsyncTCP task 上下文）。
void tick(uint32_t now_ms);

// Phase 6：序列化当前 SharedState 为 JSON 并 textAll 广播给所有 ws client。
// 每个 setter 调用末尾触发。可由 onWsEvent CONNECT 调用让新 client 接收当前 state。
void broadcastState();

// Phase 11：主循环消费 /cc 端点暂存的 Claude Code 状态事件。
// /cc handler 跑在 AsyncTCP 上下文，只暂存；切模式 / 绘屏延迟到 loop 执行（避开 async 绘屏竞争，§1.6）。
// 有待处理事件则填 out（状态）+ project_out（发起项目，?p= 解码后，可空串）+ 清标志 + 返回 true。
bool consumeCcEvent(state::CcStatus& out, char* project_out, size_t project_cap);

}  // namespace mochi::web::WebStack
