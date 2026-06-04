#include "ws_handler.h"
#include "ws_protocol.h"
#include "ap_server.h"
#include "../state.h"
#include "../mode_manager.h"
#include "../services/provisioning.h"
#include "../modes/claude_code.h"
#include "../modes/canvas.h"
#include "../faces/faces_data.h"
#include "../services/reminder.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <M5Dial.h>
#include <cctype>
#include <cstring>

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

// ── Phase 10：Settings 页（正常 STA 模式）─────────────────────────────────────

void handleSetPcIp(const JsonDocument& doc) {
    const char* ip = doc["ip"];
    if (!ip) return;
    const size_t len = strlen(ip);
    if (len == 0 || len > 15) return;
    for (size_t i = 0; i < len; ++i) {  // 宽松校验：仅点分数字
        if (!isdigit(static_cast<unsigned char>(ip[i])) && ip[i] != '.') return;
    }
    provisioning::savePcIp(ip);
    WebStack::broadcastState();
}

void handleWifiReset(const JsonDocument& /*doc*/) {
    provisioning::clearConfig();
    provisioning::requestSetupMode();  // 清凭据后直接进配网屏（默认不再自动进配网，故显式置标志）
    provisioning::requestRestart();    // 不在 WS 回调里直接 restart（§1.6）
}

void handleRestart(const JsonDocument& /*doc*/) {
    provisioning::requestRestart();
}

void handleSetCcStyle(const JsonDocument& doc) {
    const char* style = doc["style"];
    state::CcStyle st;
    if (!protocol::nameToCcStyle(style, st)) return;
    provisioning::saveCcStyle(static_cast<uint8_t>(st));  // 写 state + NVS 持久化
    // 仅当前正处于联动模式才立即应用新风格重绘（其他 mode 不读 cc_style）
    if (auto* mode = g_mochi.currentMode()) {
        if (mode->id() == ModeId::CLAUDE_STATUS) mode->applyState(state::g_state);
    }
    WebStack::broadcastState();
}

void handleSetCcScope(const JsonDocument& doc) {
    const char* scope = doc["scope"];                  // 缺省 / 空 = 全局（反映所有项目）
    provisioning::saveCcScope(scope ? scope : "");     // 写 state.cc_scope + NVS（过长自动截断）
    WebStack::broadcastState();                        // 回填 settings 选择器 + 同步其他 client
}

void handleSetFace(const JsonDocument& doc) {
    const char* key = doc["key"];
    if (!key) return;
    const uint8_t index = faces::findByKey(key);
    if (strcmp(key, faces::keyForIndex(index)) != 0) return;
    state::g_state.face_index = index;
    // 仅当前正处于 face_show 才立即重绘（其他 mode 不读 face_index）
    if (auto* mode = g_mochi.currentMode()) {
        if (mode->id() == ModeId::FACE_SHOW) mode->applyState(state::g_state);
    }
    WebStack::broadcastState();
}

// ── Phase 13：Reminder CRUD ─────────────────────────────────────────────────

void handleReminderAdd(const JsonDocument& doc) {
    if (!doc["hour"].is<int>() || !doc["minute"].is<int>()) return;
    const int hour   = doc["hour"].as<int>();
    const int minute = doc["minute"].as<int>();
    const bool daily = doc["daily"].as<bool>();
    const char* msg  = doc["msg"];
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return;
    reminder::add(static_cast<uint8_t>(hour), static_cast<uint8_t>(minute), daily, msg ? msg : "");
    WebStack::broadcastState();
}

void handleReminderDel(const JsonDocument& doc) {
    if (!doc["index"].is<int>()) return;
    const int index = doc["index"].as<int>();
    if (index < 0) return;
    reminder::removeAt(static_cast<uint8_t>(index));
    WebStack::broadcastState();
}

// ── Phase 14b：PC Monitor 面板配置 ─────────────────────────────────────────────

void handleSetMonitor(const JsonDocument& doc) {
    state::MonitorCfg mc = state::g_state.monitor;  // 以现值为基，仅覆盖出现的字段
    if (doc["rotate"].is<bool>()) mc.rotate = doc["rotate"].as<bool>();
    if (doc["interval"].is<int>()) {
        int iv = doc["interval"].as<int>();
        if (iv < 3)   iv = 3;
        if (iv > 600) iv = 600;
        mc.interval_s = static_cast<uint16_t>(iv);
    }
    uint8_t si;
    const char* single = doc["single"];
    if (single && protocol::monCatIndex(single, si)) mc.single_cat = si;
    JsonArrayConst cats = doc["cats"];
    if (!cats.isNull()) {
        uint8_t mask = 0;
        for (JsonVariantConst v : cats) {
            uint8_t ci;
            if (protocol::monCatIndex(v.as<const char*>(), ci)) mask |= (1u << ci);
        }
        if (mask != 0) mc.enabled_mask = mask;  // 不允许全不选（空则保留旧值）
    }
    provisioning::saveMonitorCfg(mc);           // 写 state.monitor + NVS
    if (auto* mode = g_mochi.currentMode()) {   // 仅当前正处于 PC Monitor 才立即重绘
        if (mode->id() == ModeId::PC_MONITOR) mode->applyState(state::g_state);
    }
    WebStack::broadcastState();
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
    else if (strcmp(type, kTypeSetPcIp)        == 0) handleSetPcIp(doc);
    else if (strcmp(type, kTypeWifiReset)      == 0) handleWifiReset(doc);
    else if (strcmp(type, kTypeRestart)        == 0) handleRestart(doc);
    else if (strcmp(type, kTypeSetCcStyle)     == 0) handleSetCcStyle(doc);
    else if (strcmp(type, kTypeSetCcScope)     == 0) handleSetCcScope(doc);
    else if (strcmp(type, kTypeSetFace)        == 0) handleSetFace(doc);
    else if (strcmp(type, kTypeReminderAdd)    == 0) handleReminderAdd(doc);
    else if (strcmp(type, kTypeReminderDel)    == 0) handleReminderDel(doc);
    else if (strcmp(type, kTypeSetMonitor)     == 0) handleSetMonitor(doc);
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
