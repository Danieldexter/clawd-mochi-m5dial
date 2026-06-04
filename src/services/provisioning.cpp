#include "provisioning.h"

#include "../config.h"
#include "../state.h"

#include <Arduino.h>
#include <M5Dial.h>
#include <WiFi.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <cstring>

namespace mochi::provisioning {

namespace {

Preferences prefs;
DNSServer   dnsServer;

bool     g_setup_mode      = false;
bool     g_restart_pending = false;
uint32_t g_restart_deadline = 0;

// NVS key（均 ≤15 字符）。namespace 在 config::kNvsNamespace。
constexpr const char* kKeySsid  = "ssid";
constexpr const char* kKeyPass  = "pass";
constexpr const char* kKeyPcip  = "pcip";
constexpr const char* kKeySetup = "setup";  // 按需配网标志（一次性：启动时读后即清）
constexpr const char* kKeyStyle = "ccstyle";  // Phase 11：联动模式视觉风格（0=CLAWD 1=SPARKLE）
constexpr const char* kKeyScope = "ccscope";  // Phase 11b：联动作用域过滤（项目 CLAUDE_PROJECT_DIR，空=全局）
constexpr const char* kKeyProjects = "ccprojects";  // Phase 11b：最近项目注册表，newline 分隔
constexpr const char* kKeyMonCfg   = "moncfg";       // Phase 14b：PC Monitor 面板配置，打包 uint32

char g_ssid[33] = {0};  // SSID 最长 32
char g_pass[65] = {0};  // WPA2 PSK 最长 64
char g_pcip[16] = {0};  // IPv4 最长 15
char g_cc_projects[config::kCcProjectsMax][config::kCcTokenLen] = {};
uint8_t g_cc_project_count = 0;

void copyTo(char* dst, size_t cap, const String& src) {
    strncpy(dst, src.c_str(), cap - 1);
    dst[cap - 1] = '\0';
}

void loadProjects(const String& raw) {
    g_cc_project_count = 0;
    int start = 0;
    while (start < raw.length() && g_cc_project_count < config::kCcProjectsMax) {
        int end = raw.indexOf('\n', start);
        if (end < 0) end = raw.length();
        const String item = raw.substring(start, end);
        if (item.length() > 0) {
            copyTo(g_cc_projects[g_cc_project_count], config::kCcTokenLen, item);
            ++g_cc_project_count;
        }
        start = end + 1;
    }
}

String serializeProjects() {
    String out;
    for (uint8_t i = 0; i < g_cc_project_count; ++i) {
        if (i) out += '\n';
        out += g_cc_projects[i];
    }
    return out;
}

}  // namespace

bool begin() {
    prefs.begin(config::kNvsNamespace, /*readOnly=*/true);
    copyTo(g_ssid, sizeof(g_ssid), prefs.getString(kKeySsid, ""));
    copyTo(g_pass, sizeof(g_pass), prefs.getString(kKeyPass, ""));
    copyTo(g_pcip, sizeof(g_pcip), prefs.getString(kKeyPcip, ""));
    const uint8_t st = prefs.getUChar(kKeyStyle, 0);
    state::g_state.cc_style = (st == 1) ? state::CcStyle::SPARKLE : state::CcStyle::CLAWD;
    copyTo(state::g_state.cc_scope, sizeof(state::g_state.cc_scope), prefs.getString(kKeyScope, ""));  // Phase 11b：联动作用域
    loadProjects(prefs.getString(kKeyProjects, ""));
    if (prefs.isKey(kKeyMonCfg)) {  // Phase 14b：回读 PC Monitor 面板配置（缺省则保留 MonitorCfg 构造默认值）
        const uint32_t v = prefs.getUInt(kKeyMonCfg, 0);
        auto& mc = state::g_state.monitor;
        mc.interval_s   = static_cast<uint16_t>(v & 0xFFFF);
        mc.single_cat   = static_cast<uint8_t>((v >> 16) & 0xFF);
        mc.enabled_mask = static_cast<uint8_t>((v >> 24) & 0x3F);
        mc.rotate       = ((v >> 30) & 0x1) != 0;
        if (mc.interval_s < 3)  mc.interval_s = 20;     // 防御越界存值
        if (mc.enabled_mask == 0) mc.enabled_mask = 0x3F;
        if (mc.single_cat >= 6) mc.single_cat = 0;
    }
    prefs.end();
    copyTo(state::g_state.pc_ip, sizeof(state::g_state.pc_ip), String(g_pcip));
    Serial.printf("[prov] loaded ssid='%s' pcip='%s'\n", g_ssid, g_pcip);
    return strlen(g_ssid) > 0;
}

bool connectSTA(uint32_t timeout_ms) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(g_ssid, g_pass);
    Serial.printf("[prov] STA connecting to '%s'...\n", g_ssid);
    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > timeout_ms) {
            Serial.println("[prov] STA connect TIMEOUT");
            return false;
        }
        delay(100);  // 喂看门狗；boot 期阻塞，非主循环（§1.6）
    }
    Serial.printf("[prov] STA connected, IP=%s\n", WiFi.localIP().toString().c_str());
    return true;
}

void startSetupAP() {
    const IPAddress ap_ip(192, 168, 4, 1);
    const IPAddress ap_gw(192, 168, 4, 1);
    const IPAddress ap_nm(255, 255, 255, 0);
    // AP_STA：STA 保持空闲（不 begin），仅为让 /scan 的 WiFi.scanNetworks 可用（WiFiManager 同款）。
    // 配网时无动画，§9 掉帧顾虑不适用。
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(ap_ip, ap_gw, ap_nm);
    const bool ok = WiFi.softAP(config::kSetupApSsid);  // 开放，无密码
    dnsServer.start(53, "*", ap_ip);                    // captive portal：所有域名 → AP IP
    WiFi.scanNetworks(/*async=*/true);                  // 预热一次异步扫描供 /scan
    g_setup_mode = true;
    Serial.printf("[prov] setup AP '%s' %s @ %s (captive portal on)\n",
                  config::kSetupApSsid, ok ? "up" : "FAILED",
                  WiFi.softAPIP().toString().c_str());
}

void startMdns() {
    if (MDNS.begin(config::kMdnsHost)) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("[prov] mDNS up: http://%s.local\n", config::kMdnsHost);
    } else {
        Serial.println("[prov] mDNS begin FAILED");
    }
}

void tickDNS() {
    dnsServer.processNextRequest();
}

bool saveConfig(const char* ssid, const char* pass, const char* pcip) {
    prefs.begin(config::kNvsNamespace, /*readOnly=*/false);
    prefs.putString(kKeySsid, ssid ? ssid : "");
    prefs.putString(kKeyPass, pass ? pass : "");
    prefs.putString(kKeyPcip, pcip ? pcip : "");
    prefs.end();  // putString 立即落盘，无需 commit
    Serial.printf("[prov] saved ssid='%s' pcip='%s'\n", ssid ? ssid : "", pcip ? pcip : "");
    return true;
}

void savePcIp(const char* ip) {
    if (!ip) return;
    prefs.begin(config::kNvsNamespace, /*readOnly=*/false);
    prefs.putString(kKeyPcip, ip);
    prefs.end();
    copyTo(g_pcip, sizeof(g_pcip), String(ip));
    copyTo(state::g_state.pc_ip, sizeof(state::g_state.pc_ip), String(ip));
    Serial.printf("[prov] pcip updated -> '%s'\n", g_pcip);
}

void saveCcStyle(uint8_t style) {
    const uint8_t v = (style == 1) ? 1 : 0;  // 仅 0/1 合法
    prefs.begin(config::kNvsNamespace, /*readOnly=*/false);
    prefs.putUChar(kKeyStyle, v);
    prefs.end();
    state::g_state.cc_style = (v == 1) ? state::CcStyle::SPARKLE : state::CcStyle::CLAWD;
    Serial.printf("[prov] cc_style updated -> %u\n", v);
}

void saveCcScope(const char* scope) {
    const char* s = scope ? scope : "";
    prefs.begin(config::kNvsNamespace, /*readOnly=*/false);
    prefs.putString(kKeyScope, s);
    prefs.end();
    copyTo(state::g_state.cc_scope, sizeof(state::g_state.cc_scope), String(s));
    Serial.printf("[prov] cc_scope -> '%s'\n", s[0] ? s : "(global)");
}

void saveMonitorCfg(const state::MonitorCfg& cfg) {
    state::g_state.monitor = cfg;  // 更新真值源（同 saveCcStyle 模式：一次调用既持久化又更新 state）
    const uint32_t v = static_cast<uint32_t>(cfg.interval_s)
                     | (static_cast<uint32_t>(cfg.single_cat & 0xFF)   << 16)
                     | (static_cast<uint32_t>(cfg.enabled_mask & 0x3F) << 24)
                     | (static_cast<uint32_t>(cfg.rotate ? 1u : 0u)    << 30);
    prefs.begin(config::kNvsNamespace, /*readOnly=*/false);
    prefs.putUInt(kKeyMonCfg, v);
    prefs.end();
    Serial.printf("[prov] monitor cfg saved: rotate=%d interval=%u single=%u mask=0x%02X\n",
                  cfg.rotate, cfg.interval_s, cfg.single_cat, cfg.enabled_mask);
}

void registerCcProject(const char* project) {
    if (!project || project[0] == '\0') return;

    char normalized[config::kCcTokenLen] = {0};
    snprintf(normalized, sizeof(normalized), "%s", project);

    int existing = -1;
    for (uint8_t i = 0; i < g_cc_project_count; ++i) {
        if (strcmp(g_cc_projects[i], normalized) == 0) {
            existing = i;
            break;
        }
    }
    if (existing == 0) return;  // 已经 newest-first，不写 NVS，减少磨损

    const uint8_t limit = config::kCcProjectsMax;
    uint8_t new_count = g_cc_project_count;
    if (existing < 0 && new_count < limit) ++new_count;
    if (new_count == 0) new_count = 1;

    const int last_to_shift = (existing > 0) ? existing : static_cast<int>(new_count - 1);
    for (int i = last_to_shift; i > 0; --i) {
        snprintf(g_cc_projects[i], config::kCcTokenLen, "%s", g_cc_projects[i - 1]);
    }
    snprintf(g_cc_projects[0], config::kCcTokenLen, "%s", normalized);
    g_cc_project_count = new_count;

    prefs.begin(config::kNvsNamespace, /*readOnly=*/false);
    prefs.putString(kKeyProjects, serializeProjects());
    prefs.end();
    Serial.printf("[prov] cc project registered -> '%s'\n", normalized);
}

uint8_t ccProjectCount() {
    return g_cc_project_count;
}

const char* ccProjectAt(uint8_t index) {
    return (index < g_cc_project_count) ? g_cc_projects[index] : "";
}

void clearConfig() {
    prefs.begin(config::kNvsNamespace, /*readOnly=*/false);
    prefs.clear();
    prefs.end();
    g_ssid[0] = g_pass[0] = g_pcip[0] = '\0';
    g_cc_project_count = 0;
    state::g_state.cc_scope[0] = '\0';
    Serial.println("[prov] config cleared");
}

void requestSetupMode() {
    prefs.begin(config::kNvsNamespace, /*readOnly=*/false);
    prefs.putBool(kKeySetup, true);
    prefs.end();
    Serial.println("[prov] setup mode requested for next boot");
}

bool consumeSetupRequest() {
    prefs.begin(config::kNvsNamespace, /*readOnly=*/false);
    const bool req = prefs.getBool(kKeySetup, false);
    if (req) prefs.remove(kKeySetup);  // 一次性：读后即清，避免卡在配网屏
    prefs.end();
    return req;
}

void requestRestart(uint32_t delay_ms) {
    g_restart_pending  = true;
    g_restart_deadline = millis() + delay_ms;
    Serial.printf("[prov] restart requested in %u ms\n", static_cast<unsigned>(delay_ms));
}

void tickRestart(uint32_t now_ms) {
    if (g_restart_pending && static_cast<int32_t>(now_ms - g_restart_deadline) >= 0) {
        Serial.println("[prov] restarting now");
        ESP.restart();
    }
}

bool        isSetupMode() { return g_setup_mode; }
const char* pcIp()        { return g_pcip; }
const char* ssid()        { return g_ssid; }

void drawSetupScreen() {
    auto& d = M5Dial.Display;
    d.fillScreen(TFT_BLACK);
    // 扫此码即加入开放配网热点，手机 captive portal 自动弹配网页。
    const String payload = String("WIFI:S:") + config::kSetupApSsid + ";T:nopass;;";
    d.qrcode(payload.c_str(), /*x=*/45, /*y=*/33, /*w=*/150, /*version=*/3);
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.setTextDatum(middle_center);
    d.setFont(&fonts::Font0);
    d.drawString("Setup", 120, 205);  // 落安全圈内（§6）
}

}  // namespace mochi::provisioning
