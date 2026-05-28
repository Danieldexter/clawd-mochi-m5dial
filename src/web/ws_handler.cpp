#include "ws_handler.h"
#include "ws_protocol.h"
#include "ap_server.h"
#include "../state.h"
#include "../mode_manager.h"
#include "../modes/claude_code.h"
#include "../modes/canvas.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <M5Dial.h>

namespace mochi::web {

namespace {

// ── 各 message type 的 handler ──────────────────────────────────────────────
//   每个 handler：修改 g_state → 触发 currentMode().applyState → broadcastState

void handleSetMode(const JsonDocument& doc) {
    const char* id_str = doc["id"];
    if (!id_str) return;
    ModeId id;
    if (!protocol::nameToModeId(id_str, id)) return;
    state::g_state.current_mode = id;
    g_mochi.setMode(id);  // onEnter 内部读 g_state 同步颜色 / 速度
    WebStack::broadcastState();
}

void handleSetSpeed(const JsonDocument& doc) {
    const uint8_t v = doc["v"].as<uint8_t>();
    if (v < 1 || v > 3) return;
    state::g_state.speed = v;
    if (auto* mode = g_mochi.currentMode()) mode->applyState(state::g_state);
    WebStack::broadcastState();
}

void handleSetBgColor(const JsonDocument& doc) {
    const char* hex = doc["hex"];
    if (!hex) return;
    state::g_state.bg_color_565 = protocol::hexToRgb565(hex);
    if (auto* mode = g_mochi.currentMode()) mode->applyState(state::g_state);
    WebStack::broadcastState();
}

void handleSetPenColor(const JsonDocument& doc) {
    const char* hex = doc["hex"];
    if (!hex) return;
    state::g_state.pen_color_565 = protocol::hexToRgb565(hex);
    // Canvas 自己 applyState 时不重绘 pen_color 变化（Phase 7 才用），只广播 UI 同步
    WebStack::broadcastState();
}

void handleSetBacklight(const JsonDocument& doc) {
    const bool on = doc["on"].as<bool>();
    state::g_state.backlight = on;
    M5Dial.Display.setBrightness(on ? state::kBacklightOnLevel
                                    : state::kBacklightOffLevel);
    WebStack::broadcastState();
}

void handleClearCanvas(const JsonDocument& doc) {
    if (state::g_state.current_mode != ModeId::CANVAS) {
        Serial.println("[WS] clear_canvas: not in canvas mode, dropped");
        return;
    }
    const char* hex = doc["hex"];
    if (hex) state::g_state.bg_color_565 = protocol::hexToRgb565(hex);
    auto* mode = g_mochi.currentMode();
    if (mode && mode->id() == ModeId::CANVAS) {
        // 直接调 Canvas::clear 无条件清屏（applyState 内有 bg 守卫会被同色 hex 跳过）
        static_cast<Canvas*>(mode)->clear(state::g_state.bg_color_565);
    }
    WebStack::broadcastState();
}

void handleTerminalInput(const JsonDocument& doc) {
    if (state::g_state.current_mode != ModeId::CLAUDE_CODE) {
        Serial.println("[WS] terminal_input: not in claude_code mode, dropped");
        return;
    }
    const char* c_str = doc["c"];
    if (!c_str || c_str[0] == '\0') return;
    const char c = c_str[0];

    auto* mode = g_mochi.currentMode();
    if (mode && mode->id() == ModeId::CLAUDE_CODE) {
        static_cast<ClaudeCode*>(mode)->inputChar(c);
    }
}

void handleStroke(const JsonDocument& doc) {
    if (state::g_state.current_mode != ModeId::CANVAS) {
        // 静默丢弃；非 canvas mode 收到 stroke 不报错
        return;
    }
    auto* mode = g_mochi.currentMode();
    if (!mode || mode->id() != ModeId::CANVAS) return;
    auto* canvas = static_cast<Canvas*>(mode);

    const int16_t x = doc["x"].as<int16_t>();
    const int16_t y = doc["y"].as<int16_t>();
    const uint16_t color = state::g_state.pen_color_565;

    if (doc["prev_x"].is<int>() && doc["prev_y"].is<int>()) {
        const int16_t px = doc["prev_x"].as<int16_t>();
        const int16_t py = doc["prev_y"].as<int16_t>();
        canvas->drawStroke(px, py, x, y, color);
    } else {
        canvas->drawDot(x, y, color);
    }
    canvas->flush();
}

void dispatchMessage(uint8_t* data, size_t len) {
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        Serial.printf("[WS] JSON parse err: %s\n", err.c_str());
        return;
    }
    const char* type = doc["type"];
    if (!type) {
        Serial.println("[WS] message missing 'type'");
        return;
    }

    using namespace protocol;
    if      (strcmp(type, kTypeSetMode)        == 0) handleSetMode(doc);
    else if (strcmp(type, kTypeSetSpeed)       == 0) handleSetSpeed(doc);
    else if (strcmp(type, kTypeSetBgColor)     == 0) handleSetBgColor(doc);
    else if (strcmp(type, kTypeSetPenColor)    == 0) handleSetPenColor(doc);
    else if (strcmp(type, kTypeSetBacklight)   == 0) handleSetBacklight(doc);
    else if (strcmp(type, kTypeClearCanvas)    == 0) handleClearCanvas(doc);
    else if (strcmp(type, kTypeTerminalInput)  == 0) handleTerminalInput(doc);
    else if (strcmp(type, kTypeStroke)         == 0) handleStroke(doc);
    else Serial.printf("[WS] unknown type: %s\n", type);
}

}  // namespace

void onWsEvent(AsyncWebSocket* /*server*/, AsyncWebSocketClient* client,
               AwsEventType type, void* arg, uint8_t* data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("[WS] Client %u connected\n", client->id());
            WebStack::broadcastState();  // 让新 client 立刻拿到当前 state
            break;
        case WS_EVT_DISCONNECT:
            Serial.printf("[WS] Client %u disconnected\n", client->id());
            break;
        case WS_EVT_DATA: {
            auto* info = static_cast<AwsFrameInfo*>(arg);
            if (info->final && info->index == 0 && info->len == len) {
                dispatchMessage(data, len);
            }
            break;
        }
        case WS_EVT_PONG:
            break;
        case WS_EVT_ERROR:
            Serial.printf("[WS] Client %u error\n", client->id());
            break;
    }
}

}  // namespace mochi::web
