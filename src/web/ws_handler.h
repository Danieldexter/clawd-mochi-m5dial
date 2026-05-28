#pragma once

#include <ESPAsyncWebServer.h>

namespace mochi::web {

// WebSocket 事件回调。Phase 5 仅 log；Phase 6 在 WS_EVT_DATA 填消息协议解析。
void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
               AwsEventType type, void* arg, uint8_t* data, size_t len);

}  // namespace mochi::web
