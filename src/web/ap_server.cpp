#include "ap_server.h"
#include "ws_handler.h"
#include "ws_protocol.h"
#include "../config.h"
#include "../state.h"
#include "../mode_manager.h"

#include <WiFi.h>
#include <LittleFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

namespace mochi::web {

namespace {

AsyncWebServer server(80);
AsyncWebSocket ws(config::kWsPath);

constexpr uint32_t kCleanupIntervalMs = 1000;
uint32_t           last_cleanup_ms    = 0;

bool fs_ok = false;

// 序列化当前 SharedState 为 JSON 字符串
String buildStateJson() {
    JsonDocument doc;
    doc["type"]      = protocol::kTypeState;
    doc["mode"]      = protocol::modeIdToName(state::g_state.current_mode);
    doc["speed"]     = state::g_state.speed;

    char hex_bg[8], hex_pen[8];
    protocol::rgb565ToHex(state::g_state.bg_color_565,  hex_bg);
    protocol::rgb565ToHex(state::g_state.pen_color_565, hex_pen);
    doc["bg_color"]  = hex_bg;
    doc["pen_color"] = hex_pen;
    doc["backlight"] = state::g_state.backlight;
    doc["term_mode"] = (state::g_state.current_mode == ModeId::CLAUDE_CODE);

    String out;
    serializeJson(doc, out);
    return out;
}

}  // namespace

namespace WebStack {

void begin() {
    fs_ok = LittleFS.begin(/*format_on_fail=*/true);
    if (!fs_ok) {
        Serial.println("[fs] LittleFS mount FAILED (serveStatic will be skipped)");
    } else {
        Serial.printf("[fs] LittleFS mounted, total=%u used=%u\n",
                      static_cast<unsigned>(LittleFS.totalBytes()),
                      static_cast<unsigned>(LittleFS.usedBytes()));
    }

    const IPAddress ap_ip(192, 168, 4, 1);
    const IPAddress ap_gw(192, 168, 4, 1);
    const IPAddress ap_nm(255, 255, 255, 0);
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(ap_ip, ap_gw, ap_nm);
    const bool ap_ok = WiFi.softAP(config::kApSsid, config::kApPassword);
    if (!ap_ok) {
        Serial.println("[wifi] softAP FAILED (continuing without WiFi)");
    } else {
        Serial.printf("[wifi] AP %s up @ %s\n",
                      config::kApSsid,
                      WiFi.softAPIP().toString().c_str());
    }

    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    if (fs_ok) {
        server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    }
    server.onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "text/plain", "404");
    });
    server.begin();
    Serial.printf("[http] AsyncWebServer up :80 (WS path %s)\n", config::kWsPath);
}

void tick(uint32_t now_ms) {
    if (now_ms - last_cleanup_ms >= kCleanupIntervalMs) {
        last_cleanup_ms = now_ms;
        ws.cleanupClients();
    }
}

void broadcastState() {
    if (ws.count() == 0) return;  // 没 client 不浪费 CPU
    const String s = buildStateJson();
    ws.textAll(s);
}

}  // namespace WebStack

}  // namespace mochi::web
