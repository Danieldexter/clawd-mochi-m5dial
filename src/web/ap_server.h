#pragma once

#include <Arduino.h>

namespace mochi::web::WebStack {

// 启动 LittleFS、WiFi AP、AsyncWebServer + AsyncWebSocket。失败时降级（log + 跳过对应模块），
// 不阻塞 mode_manager 主循环。CLAUDE.md §1.6。
void begin();

// 在主循环里定期调用。当前仅 1 Hz 跑 `ws.cleanupClients()`。
// 必须在 loop 线程调（不要在 onWsEvent 回调里调，那个跑在 AsyncTCP task 上下文）。
void tick(uint32_t now_ms);

// Phase 6：序列化当前 SharedState 为 JSON 并 textAll 广播给所有 ws client。
// 每个 setter 调用末尾触发。可由 onWsEvent CONNECT 调用让新 client 接收当前 state。
void broadcastState();

}  // namespace mochi::web::WebStack
