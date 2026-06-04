#include "ap_server.h"
#include "ws_handler.h"
#include "ws_protocol.h"
#include "../config.h"
#include "../state.h"
#include "../mode_manager.h"
#include "../faces/faces_data.h"
#include "../services/provisioning.h"
#include "../services/reminder.h"
#include "../modes/pc_monitor.h"   // pcmon::availableMask（Phase 14b state 广播）

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

// Phase 11：/cc 端点暂存（AsyncTCP 上下文写，loop 读；volatile 防编译器缓存）。
// 只存最新状态 —— 多事件间隔小于一次消费时，保留最近的即正确语义。
volatile bool            g_cc_pending = false;
volatile state::CcStatus g_pending_cc = state::CcStatus::IDLE;
char                     g_pending_project[config::kCcTokenLen] = {0};  // 最近一次 /cc 的 ?p=（AsyncTCP 写、loop 读）

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
    doc["pc_ip"]     = state::g_state.pc_ip;          // Phase 10：Settings 页展示
    doc["ssid"]      = provisioning::ssid();          // 当前连接的 WiFi（只读展示）
    doc["cc_status"] = protocol::ccStatusToName(state::g_state.cc_status);  // Phase 11：联动状态徽标
    doc["cc_style"]  = protocol::ccStyleToName(state::g_state.cc_style);    // Phase 11：联动模式风格（Settings 选择器回填）
    doc["cc_scope"]  = state::g_state.cc_scope;                             // Phase 11b：当前联动项目作用域（Settings 下拉回填）
    JsonArray projects = doc["cc_projects"].to<JsonArray>();                 // Phase 11b：最近 /cc?p= 项目注册表
    for (uint8_t i = 0; i < provisioning::ccProjectCount(); ++i) {
        projects.add(provisioning::ccProjectAt(i));
    }
    doc["face_key"]  = faces::keyForIndex(state::g_state.face_index);        // Phase 12：face_show 当前表情 key（前端选择器回填）

    JsonArray reminders = doc["reminders"].to<JsonArray>();                   // Phase 13：提醒列表（Web 渲染 + 回填）
    for (uint8_t i = 0; i < reminder::count(); ++i) {
        const auto* r = reminder::get(i);
        if (!r) continue;
        JsonObject o = reminders.add<JsonObject>();
        o["hour"]   = r->hour;
        o["minute"] = r->minute;
        o["daily"]  = r->daily;
        o["msg"]    = r->msg;
    }

    // Phase 14b：PC Monitor 面板配置 + 当前 PC 上报的可用分类（web 渲染配置 UI + 置灰用）
    JsonObject mon = doc["monitor"].to<JsonObject>();
    mon["rotate"]   = state::g_state.monitor.rotate;
    mon["interval"] = state::g_state.monitor.interval_s;
    mon["single"]   = protocol::monCatName(state::g_state.monitor.single_cat);
    JsonArray cats  = mon["cats"].to<JsonArray>();         // 轮询启用的分类
    for (uint8_t i = 0; i < protocol::kMonCatCount; ++i) {
        if (state::g_state.monitor.enabled_mask & (1u << i)) cats.add(protocol::monCatName(i));
    }
    JsonArray avail = mon["available"].to<JsonArray>();    // PC 当前有数据的分类
    const uint8_t av = pcmon::availableMask();
    for (uint8_t i = 0; i < protocol::kMonCatCount; ++i) {
        if (av & (1u << i)) avail.add(protocol::monCatName(i));
    }

    String out;
    serializeJson(doc, out);
    return out;
}

// ── 配网模式 HTTP handler（Phase 9，captive portal 下走原生 HTTP）──────────────

// GET /scan：返回邻近网络 SSID JSON 数组。用异步扫描结果（非阻塞 AsyncTCP task）。
void handleScan(AsyncWebServerRequest* req) {
    const int16_t n = WiFi.scanComplete();
    if (n < 0) {                       // 扫描中(-1)或失败(-2)：返回空 + 重新预热
        if (n == WIFI_SCAN_FAILED) WiFi.scanNetworks(/*async=*/true);
        req->send(200, "application/json", "[]");
        return;
    }
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int16_t i = 0; i < n; ++i) arr.add(WiFi.SSID(i));
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
    WiFi.scanDelete();
    WiFi.scanNetworks(/*async=*/true);  // 预热下一次
}

// GET /save?ssid=&pass=&pcip=：写 NVS → 回成功页 → 延迟重启进 STA。
void handleSave(AsyncWebServerRequest* req) {
    const String ssid = req->hasParam("ssid") ? req->getParam("ssid")->value() : "";
    const String pass = req->hasParam("pass") ? req->getParam("pass")->value() : "";
    const String pcip = req->hasParam("pcip") ? req->getParam("pcip")->value() : "";
    if (ssid.isEmpty()) {
        req->send(400, "text/html; charset=utf-8",
                  "<meta charset=utf-8><p>SSID required. <a href='/'>back</a></p>");
        return;
    }
    provisioning::saveConfig(ssid.c_str(), pass.c_str(), pcip.c_str());

    String html = "<!doctype html><meta charset=utf-8>"
                  "<meta name=viewport content='width=device-width,initial-scale=1'>"
                  "<style>body{font:16px system-ui;background:#1a1a1a;color:#ddd;"
                  "text-align:center;padding:40px 20px}b{color:#5dd}</style>"
                  "<h2>Saved &#10003;</h2>"
                  "<p>Rebooting. Reconnect to your home WiFi, then open</p>"
                  "<p><b>http://";
    html += config::kMdnsHost;
    html += ".local</b></p>";
    req->send(200, "text/html; charset=utf-8", html);

    provisioning::requestRestart(800);  // 给响应发出的时间，主循环到点重启
}

// GET /cc?s={working|waiting|idle}：Claude Code hook 推状态。只暂存 + 置标志，立即回 200；
// 切模式 / 绘屏由 loop 的 consumeCcEvent 处理（避开 AsyncTCP 上下文直接绘屏，§1.6）。
void handleCcStatus(AsyncWebServerRequest* req) {
    const String s = req->hasParam("s") ? req->getParam("s")->value() : "";
    state::CcStatus st;
    if      (s == "working") st = state::CcStatus::WORKING;
    else if (s == "waiting") st = state::CcStatus::WAITING;
    else if (s == "idle")    st = state::CcStatus::IDLE;
    else { req->send(400, "text/plain", "bad s (working|waiting|idle)"); return; }
    // Phase 11b：可选项目标识（?p=，hook 传 $CLAUDE_PROJECT_DIR）。先写串再置 publish 标志。
    const String p = req->hasParam("p") ? req->getParam("p")->value() : "";
    snprintf(g_pending_project, sizeof(g_pending_project), "%s", p.c_str());
    g_pending_cc = st;
    g_cc_pending = true;
    req->send(200, "text/plain", "ok");
}

}  // namespace

namespace WebStack {

void begin(bool setup_mode) {
    fs_ok = LittleFS.begin(/*format_on_fail=*/true);
    if (!fs_ok) {
        Serial.println("[fs] LittleFS mount FAILED (serveStatic will be skipped)");
    } else {
        Serial.printf("[fs] LittleFS mounted, total=%u used=%u\n",
                      static_cast<unsigned>(LittleFS.totalBytes()),
                      static_cast<unsigned>(LittleFS.usedBytes()));
    }

    if (setup_mode) {
        // 配网模式：setup.html + captive portal。不挂 WS。
        if (fs_ok) {
            server.serveStatic("/", LittleFS, "/").setDefaultFile("setup.html");
        }
        server.on("/scan", HTTP_GET, handleScan);
        server.on("/save", HTTP_GET, handleSave);
        server.onNotFound([](AsyncWebServerRequest* req) {
            req->redirect("/");  // captive portal：未知路径（含 OS 探测）→ 配网页
        });
    } else {
        // 正常模式：index.html / settings.html + WS 控制管道。
        ws.onEvent(onWsEvent);
        server.addHandler(&ws);
        server.on("/cc", HTTP_GET, handleCcStatus);  // Phase 11：Claude Code hook 推状态
        if (fs_ok) {
            server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
        }
        server.onNotFound([](AsyncWebServerRequest* req) {
            req->send(404, "text/plain", "404");
        });
    }

    server.begin();
    Serial.printf("[http] AsyncWebServer up :80 (%s mode, WS path %s)\n",
                  setup_mode ? "setup" : "normal", config::kWsPath);
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

bool consumeCcEvent(state::CcStatus& out, char* project_out, size_t project_cap) {
    if (!g_cc_pending) return false;
    out = g_pending_cc;
    if (project_out && project_cap) snprintf(project_out, project_cap, "%s", g_pending_project);
    g_cc_pending = false;
    return true;
}

}  // namespace WebStack

}  // namespace mochi::web
