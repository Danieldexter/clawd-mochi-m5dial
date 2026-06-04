#include "pc_monitor.h"

#include "../config.h"
#include "../state.h"
#include "../services/provisioning.h"

#include <Arduino.h>
#include <M5Dial.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace mochi {

namespace {

// ── 调色板索引（4bpp；深底 #1c1c20 上文字奶白/白，每类强调色 kAccent 逐卡重涂）──
constexpr uint8_t kBg     = 0;   // 深色基底
constexpr uint8_t kInk    = 1;   // 主数值（cream 0xFF38）
constexpr uint8_t kWhite  = 2;   // 时钟（纯白）
constexpr uint8_t kDim    = 3;   // 标签 / 离线（浅灰 0xBDF7）
constexpr uint8_t kTrack  = 4;   // 弧环 / 进度条底槽（深灰 0x39C8）
constexpr uint8_t kAccent = 5;   // 当前卡强调色（redraw 每卡 setPal565）

constexpr uint16_t kBgDark = 0x18E4;   // #1c1c20

// ── 分类（索引 = 位序；须与 ws_protocol::kMonCatNames 同序）──
enum { CAT_CPU = 0, CAT_DISK, CAT_GPU, CAT_HOST, CAT_NET, CAT_TRAF };
const char* const kCatTitle[6] = {"CPU", "DISK", "GPU", "HOST", "NETWORK", "TRAFFIC"};
const char* const kCatWire[6]  = {"cpu", "disk", "gpu", "host", "net", "traf"};
// 每类强调色 RGB565（上机可调，仿 phase11 risk#12）：cpu橙红 disk金 gpu紫 host蓝 net绿 traf青
constexpr uint16_t kAccent565[6] = {0xFA00, 0xF5A8, 0xA3FE, 0x5D1C, 0x3DD0, 0x4E78};

// ── 圆屏几何（§6 安全圈 r≤110，圆心 120,120；坐标上机微调）──
constexpr int16_t kCx = 120, kCy = 120;
constexpr int16_t kClockY   = 30;    // 时钟 HH:MM（top_center；下移与 rim 弧顶 y≈20 拉开间距）
constexpr int16_t kTitleY   = 54;    // 分类标题（top_center，accent）
constexpr int16_t kRingR0   = 100;   // rim 弧仪表内半径
constexpr int16_t kRingR1   = 108;   // rim 弧仪表外半径（≤110）
constexpr int16_t kGaugeStart = 135; // 弧起点角（LovyanGFX：0°=3 点钟，顺时针）→ 缺口落正下方
constexpr int16_t kGaugeSweep = 270; // 弧总扫角（底部留 90° 给圆点）
constexpr int16_t kBigY     = 98;    // 中心大数值（middle_center，Bold24）
constexpr int16_t kBigLabY  = 126;   // 大数值下方小标签
// 下半区统一为两槽 line1=154 / line2=180：有 bar 卡 bar 占 line1、文字行落 line2；
// 无 bar 卡两行分落 line1/line2 —— 保证轮询切卡时行位不跳、行距一致
constexpr int16_t kBarX     = 55, kBarY = 148, kBarW = 130, kBarH = 12;  // 次级进度条（中心≈line1=154）
constexpr int16_t kRowY     = 180;   // 单行读数（有 bar 的卡）= line2
constexpr int16_t kRow1Y    = 154;   // line1（无 bar 卡第一行）
constexpr int16_t kRow2Y    = 180;   // line2（无 bar 卡第二行；与 kRowY 对齐）
constexpr int16_t kDotsY    = 206;   // 底部圆点

// ── 轮询配置 ──
constexpr uint32_t kPollIntervalMs = 5000;   // 实时速率：5s 拉一次（PC 端 2s 采样）
constexpr uint32_t kPollRetryMs    = 5000;   // 首帧前 / 离线时重试
constexpr uint16_t kHttpTimeoutMs  = 3000;
constexpr uint16_t kPcPort         = 8080;

float g_net_peak = 1.0f;   // Network 卡弧环缩放用的会话峰值（onEnter 重置）

// ── snapshot：poll task 单写、loop tick 单读，taskENTER_CRITICAL 保护拷贝 ──
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
struct Snapshot {
    bool valid = false, online = false;
    int  hour = 0, minute = 0;
    uint8_t av = 0;   // 位掩码：bit i = 分类 i 有数据
    struct { float ld = NAN, tp = NAN, fq = NAN, pw = NAN, fan = NAN; int co = -1; } cpu;
    struct { float ld = NAN, tp = NAN, vu = NAN, vt = NAN, ck = NAN, pw = NAN, fan = NAN; } gpu;
    struct { float mem = NAN, swp = NAN, tp = NAN, fan = NAN; char up[16] = {0}; } host;
    struct { float use = NAN, rd = NAN, wr = NAN, tp = NAN; } disk;
    struct { float up = NAN, dn = NAN; char nic[20] = {0}; } net;
    struct { float up = NAN, dn = NAN; } traf;
    uint32_t seq = 0;
};
Snapshot g_snap;

volatile bool g_active   = false;
volatile bool g_poll_now = false;
TaskHandle_t  g_task     = nullptr;

inline bool  na(float v) { return std::isnan(v); }
inline float jnum(JsonVariantConst v) { return v | (float)NAN; }   // 缺/null/非数 → NAN

// 解析 stats.json v2（嵌套）写入 snapshot（task 上下文）。失败保留旧值。
void parseAndStore(const String& body) {
    JsonDocument doc;
    if (deserializeJson(doc, body)) return;

    Snapshot s;
    s.valid = true;
    s.online = true;
    s.hour   = doc["clk"]["h"] | 0;
    s.minute = doc["clk"]["m"] | 0;
    for (uint8_t i = 0; i < 6; ++i) {
        if (doc["av"][kCatWire[i]] | false) s.av |= (1u << i);
    }
    s.cpu.ld = jnum(doc["cpu"]["ld"]); s.cpu.tp = jnum(doc["cpu"]["tp"]);
    s.cpu.fq = jnum(doc["cpu"]["fq"]); s.cpu.pw = jnum(doc["cpu"]["pw"]);
    s.cpu.fan = jnum(doc["cpu"]["fan"]); s.cpu.co = doc["cpu"]["co"] | -1;

    s.gpu.ld = jnum(doc["gpu"]["ld"]); s.gpu.tp = jnum(doc["gpu"]["tp"]);
    s.gpu.vu = jnum(doc["gpu"]["vu"]); s.gpu.vt = jnum(doc["gpu"]["vt"]);
    s.gpu.ck = jnum(doc["gpu"]["ck"]); s.gpu.pw = jnum(doc["gpu"]["pw"]);
    s.gpu.fan = jnum(doc["gpu"]["fan"]);

    s.host.mem = jnum(doc["host"]["mem"]); s.host.swp = jnum(doc["host"]["swp"]);
    s.host.tp  = jnum(doc["host"]["tp"]);  s.host.fan = jnum(doc["host"]["fan"]);
    snprintf(s.host.up, sizeof(s.host.up), "%s", (const char*)(doc["host"]["up"] | ""));

    s.disk.use = jnum(doc["disk"]["use"]); s.disk.rd = jnum(doc["disk"]["rd"]);
    s.disk.wr  = jnum(doc["disk"]["wr"]);  s.disk.tp = jnum(doc["disk"]["tp"]);

    s.net.up = jnum(doc["net"]["up"]); s.net.dn = jnum(doc["net"]["dn"]);
    snprintf(s.net.nic, sizeof(s.net.nic), "%s", (const char*)(doc["net"]["nic"] | ""));

    s.traf.up = jnum(doc["traf"]["up"]); s.traf.dn = jnum(doc["traf"]["dn"]);

    taskENTER_CRITICAL(&g_mux);
    const uint32_t next = g_snap.seq + 1;
    g_snap = s;
    g_snap.seq = next;
    taskEXIT_CRITICAL(&g_mux);
}

// 拉一次 stats.json。返回是否成功（200 + 已解析）。
bool pollOnce() {
    const char* ip = provisioning::pcIp();
    if (!ip || ip[0] == '\0') return false;
    if (WiFi.status() != WL_CONNECTED) return false;

    char url[80];
    snprintf(url, sizeof(url), "http://%s:%u/stats.json", ip, static_cast<unsigned>(kPcPort));

    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(kHttpTimeoutMs);
    http.setTimeout(kHttpTimeoutMs);
    if (!http.begin(client, url)) return false;
    const int code = http.GET();
    bool ok = false;
    if (code == 200) { parseAndStore(http.getString()); ok = true; }
    http.end();
    return ok;
}

// 常驻轮询 task：仅 PC Monitor 模式下 poll；首帧前/离线 5s 重试。
void pollTask(void*) {
    uint32_t last_poll = 0;
    for (;;) {
        if (g_active) {
            const uint32_t interval = g_snap.valid ? kPollIntervalMs : kPollRetryMs;
            const uint32_t now = millis();
            if (g_poll_now || (now - last_poll) >= interval) {
                g_poll_now = false;
                const bool ok = pollOnce();
                last_poll = millis();
                if (!ok) {  // 上线→离线翻转才 bump seq（重绘出离线态，避免持续 churn）
                    taskENTER_CRITICAL(&g_mux);
                    if (g_snap.online) { g_snap.online = false; ++g_snap.seq; }
                    taskEXIT_CRITICAL(&g_mux);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// RGB565 → fb_ 调色板索引（同 face_show / claude_status）。
inline void setPal565(M5Canvas& fb, uint8_t idx, uint16_t c) {
    const uint8_t r5 = (c >> 11) & 0x1F, g6 = (c >> 5) & 0x3F, b5 = c & 0x1F;
    fb.setPaletteColor(idx, static_cast<uint8_t>((r5 << 3) | (r5 >> 2)),
                            static_cast<uint8_t>((g6 << 2) | (g6 >> 4)),
                            static_cast<uint8_t>((b5 << 3) | (b5 >> 2)));
}

// 环形弧段（跨 360 回绕），画到 fb（圆心 120,120）。col=调色板索引。
void fillArcWrap(M5Canvas& fb, int16_t r0, int16_t r1, int16_t startDeg, int16_t widthDeg, uint8_t col) {
    if (widthDeg <= 0) return;
    int16_t s = ((startDeg % 360) + 360) % 360;
    int16_t e = s + widthDeg;
    if (e <= 360) fb.fillArc(kCx, kCy, r0, r1, s, e, col);
    else { fb.fillArc(kCx, kCy, r0, r1, s, 360, col);
           fb.fillArc(kCx, kCy, r0, r1, 0, e - 360, col); }
}

// ── 小工具 ──
inline float clampPct(float p) { return p < 0 ? 0 : (p > 100 ? 100 : p); }
// 数值按 fmt（含单位）格式化；NA → "--"
void tok(char* b, size_t n, float v, const char* fmt) {
    if (na(v)) snprintf(b, n, "--"); else snprintf(b, n, fmt, v);
}
void pctText(char* b, size_t n, float v) {
    if (na(v)) snprintf(b, n, "--"); else snprintf(b, n, "%d%%", (int)lroundf(v));
}
void fmtBytes(char* b, size_t n, float mb) {   // 输入 MB → "4.0G" / "512M" / "--"
    if (na(mb)) { snprintf(b, n, "--"); return; }
    if (mb >= 1024.0f) snprintf(b, n, "%.1fG", mb / 1024.0f);
    else               snprintf(b, n, "%.0fM", mb);
}

// ── 共享绘制 helper（全部画进 fb；用当前已设的 kAccent）──
void drawGauge(M5Canvas& fb, float pct) {
    fillArcWrap(fb, kRingR0, kRingR1, kGaugeStart, kGaugeSweep, kTrack);
    if (!na(pct)) {
        const int16_t w = (int16_t)(kGaugeSweep * clampPct(pct) / 100.0f);
        fillArcWrap(fb, kRingR0, kRingR1, kGaugeStart, w, kAccent);
    }
}
void drawBar(M5Canvas& fb, int16_t x, int16_t y, int16_t w, int16_t h, float pct) {
    fb.fillRoundRect(x, y, w, h, h / 2, kTrack);
    if (!na(pct)) {
        int16_t fw = (int16_t)(w * clampPct(pct) / 100.0f);
        if (fw < h) fw = h;            // 至少画出圆角端
        fb.fillRoundRect(x, y, fw, h, h / 2, kAccent);
    }
}
void drawBig(M5Canvas& fb, const char* t) {
    fb.setTextColor(kInk); fb.setTextDatum(middle_center);
    fb.setFont(&fonts::FreeSansBold24pt7b); fb.drawString(t, kCx, kBigY);
}
void drawBigLabel(M5Canvas& fb, const char* t) {
    fb.setTextColor(kDim); fb.setTextDatum(middle_center);
    fb.setFont(&fonts::FreeSans9pt7b); fb.drawString(t, kCx, kBigLabY);
}
void drawRow(M5Canvas& fb, int16_t y, const char* t) {
    fb.setTextColor(kInk); fb.setTextDatum(middle_center);
    fb.setFont(&fonts::FreeSans9pt7b); fb.drawString(t, kCx, y);
}
void drawClock(M5Canvas& fb, const Snapshot& s) {
    char b[8]; snprintf(b, sizeof(b), "%02d:%02d", s.hour, s.minute);
    fb.setTextColor(s.online ? kWhite : kDim); fb.setTextDatum(top_center);
    fb.setFont(&fonts::FreeSans9pt7b); fb.drawString(b, kCx, kClockY);
}
void drawTitle(M5Canvas& fb, const char* t) {
    fb.setTextColor(kAccent); fb.setTextDatum(top_center);
    fb.setFont(&fonts::FreeSans12pt7b); fb.drawString(t, kCx, kTitleY);
}
void drawDots(M5Canvas& fb, uint8_t mask, uint8_t active) {
    int n = 0, pos = 0;
    for (int i = 0; i < 6; ++i) if (mask & (1 << i)) { if (i == active) pos = n; ++n; }
    if (n <= 1) return;
    const int gap = 14, x0 = kCx - (n - 1) * gap / 2;
    for (int i = 0; i < n; ++i) {
        const bool on = (i == pos);
        fb.fillCircle(x0 + i * gap, kDotsY, on ? 4 : 3, on ? kAccent : kDim);
    }
}
void drawHint(M5Canvas& fb, const char* l1, const char* l2) {
    fb.setTextColor(kInk); fb.setTextDatum(middle_center);
    fb.setFont(&fonts::FreeSans12pt7b); fb.drawString(l1, kCx, 110);
    if (l2 && l2[0]) {
        fb.setTextColor(kDim); fb.setFont(&fonts::FreeSans9pt7b);
        fb.drawString(l2, kCx, 140);
    }
}

// ── 6 卡片 body（chrome 已由 redraw 画好，kAccent 已设）──
void drawCpu(M5Canvas& fb, const Snapshot& s) {
    drawGauge(fb, s.cpu.ld);
    char big[8]; pctText(big, sizeof(big), s.cpu.ld); drawBig(fb, big); drawBigLabel(fb, "LOAD");
    drawBar(fb, kBarX, kBarY, kBarW, kBarH, s.cpu.tp);   // 进度条 = 温度 0-100°C
    char row[28], a[8], b[10], c[8];
    tok(a, sizeof(a), s.cpu.tp, "%.0fC");
    tok(b, sizeof(b), na(s.cpu.fq) ? NAN : s.cpu.fq / 1000.0f, "%.1fGHz");
    tok(c, sizeof(c), s.cpu.pw, "%.0fW");
    snprintf(row, sizeof(row), "%s  %s  %s", a, b, c);
    drawRow(fb, kRowY, row);
}
void drawGpu(M5Canvas& fb, const Snapshot& s) {
    drawGauge(fb, s.gpu.ld);
    char big[8]; pctText(big, sizeof(big), s.gpu.ld); drawBig(fb, big); drawBigLabel(fb, "LOAD");
    const float vpct = (!na(s.gpu.vu) && !na(s.gpu.vt) && s.gpu.vt > 0)
                       ? s.gpu.vu / s.gpu.vt * 100.0f : NAN;
    drawBar(fb, kBarX, kBarY, kBarW, kBarH, vpct);        // 进度条 = 显存占用
    char row[30], a[8], vram[14], c[8];
    tok(a, sizeof(a), s.gpu.tp, "%.0fC");
    if (na(s.gpu.vu) || na(s.gpu.vt)) snprintf(vram, sizeof(vram), "--");
    else snprintf(vram, sizeof(vram), "%.1f/%.1fG", s.gpu.vu / 1024.0f, s.gpu.vt / 1024.0f);
    tok(c, sizeof(c), s.gpu.pw, "%.0fW");
    snprintf(row, sizeof(row), "%s  %s  %s", a, vram, c);
    drawRow(fb, kRowY, row);
}
void drawHost(M5Canvas& fb, const Snapshot& s) {
    drawGauge(fb, s.host.mem);
    char big[8]; pctText(big, sizeof(big), s.host.mem); drawBig(fb, big); drawBigLabel(fb, "MEMORY");
    drawBar(fb, kBarX, kBarY, kBarW, kBarH, s.host.swp); // 进度条 = swap 占用
    char row[28], a[8], b[10], up[10];
    tok(a, sizeof(a), s.host.tp, "%.0fC");
    tok(b, sizeof(b), s.host.fan, "fan%.0f");
    snprintf(up, sizeof(up), "%s", s.host.up[0] ? s.host.up : "--");
    snprintf(row, sizeof(row), "%s  %s  %s", a, b, up);
    drawRow(fb, kRowY, row);
}
void drawDisk(M5Canvas& fb, const Snapshot& s) {
    drawGauge(fb, s.disk.use);
    char big[8]; pctText(big, sizeof(big), s.disk.use); drawBig(fb, big); drawBigLabel(fb, "USED");
    char r1[26], a[8], b[8], r2[12], t[8];
    tok(a, sizeof(a), s.disk.rd, "%.1f");
    tok(b, sizeof(b), s.disk.wr, "%.1f");
    snprintf(r1, sizeof(r1), "R %s  W %s MB/s", a, b); drawRow(fb, kRow1Y, r1);
    tok(t, sizeof(t), s.disk.tp, "%.0f");
    snprintf(r2, sizeof(r2), "TEMP %sC", t);            drawRow(fb, kRow2Y, r2);
}
void drawNet(M5Canvas& fb, const Snapshot& s) {
    if (!na(s.net.dn) && s.net.dn > g_net_peak) g_net_peak = s.net.dn;
    if (!na(s.net.up) && s.net.up > g_net_peak) g_net_peak = s.net.up;
    drawGauge(fb, na(s.net.dn) ? NAN : s.net.dn / g_net_peak * 100.0f);
    char big[10]; tok(big, sizeof(big), s.net.dn, "%.1f"); drawBig(fb, big); drawBigLabel(fb, "DOWN MB/s");
    char r1[18], u[10]; tok(u, sizeof(u), s.net.up, "%.1f");
    snprintf(r1, sizeof(r1), "UP %s", u); drawRow(fb, kRow1Y, r1);
    drawRow(fb, kRow2Y, s.net.nic[0] ? s.net.nic : "--");
}
void drawTraf(M5Canvas& fb, const Snapshot& s) {
    const float dn = s.traf.dn, up = s.traf.up;
    const float tot = (na(dn) ? 0 : dn) + (na(up) ? 0 : up);
    drawGauge(fb, (tot > 0 && !na(dn)) ? dn / tot * 100.0f : NAN);
    char big[10]; fmtBytes(big, sizeof(big), dn); drawBig(fb, big); drawBigLabel(fb, "DOWN TODAY");
    char r1[14], u[10]; fmtBytes(u, sizeof(u), up);
    snprintf(r1, sizeof(r1), "UP %s", u); drawRow(fb, kRow1Y, r1);
}

}  // namespace

namespace pcmon {
uint8_t availableMask() {
    taskENTER_CRITICAL(&g_mux);
    const uint8_t a = g_snap.valid ? g_snap.av : 0;
    taskEXIT_CRITICAL(&g_mux);
    return a;
}
}  // namespace pcmon

void PcMonitor::applyPalette() {
    setPal565(fb_, kBg,     kBgDark);
    setPal565(fb_, kInk,    0xFF38);   // cream
    setPal565(fb_, kWhite,  0xFFFF);
    setPal565(fb_, kDim,    0xBDF7);
    setPal565(fb_, kTrack,  0x39C8);   // #3a3a40
    setPal565(fb_, kAccent, kAccent565[0]);  // 初值，redraw 每卡重涂
}

void PcMonitor::syncCfg() {
    const auto& mc = state::g_state.monitor;
    rotate_       = mc.rotate;
    interval_ms_  = static_cast<uint32_t>(mc.interval_s) * 1000u;
    single_cat_   = mc.single_cat;
    enabled_mask_ = mc.enabled_mask;
    if (interval_ms_ < 3000)  interval_ms_ = 20000;
    if (enabled_mask_ == 0)   enabled_mask_ = 0x3F;
    if (single_cat_ >= 6)     single_cat_ = 0;
}

void PcMonitor::pickInitialCard() {
    if (!rotate_) { active_cat_ = single_cat_; return; }
    const uint8_t mask = enabled_mask_ & pcmon::availableMask();
    if (mask & (1u << active_cat_)) return;             // 当前仍有效则保留
    for (int i = 0; i < 6; ++i) if (mask & (1u << i)) { active_cat_ = (uint8_t)i; return; }
    // 全不可用：保留 active_cat_（redraw 显 "no data"）
}

void PcMonitor::advanceCard() {
    const uint8_t mask = enabled_mask_ & pcmon::availableMask();
    if (!mask) return;
    uint8_t c = active_cat_;
    for (int k = 0; k < 6; ++k) { c = (uint8_t)((c + 1) % 6); if (mask & (1u << c)) { active_cat_ = c; return; } }
}

void PcMonitor::redraw() {
    if (!fb_ready_) return;

    Snapshot s;
    taskENTER_CRITICAL(&g_mux);
    s = g_snap;
    taskEXIT_CRITICAL(&g_mux);

    fb_.fillSprite(kBg);

    const char* ip = provisioning::pcIp();
    if (!ip || ip[0] == '\0') { drawHint(fb_, "Set PC IP", "in Settings"); fb_.pushSprite(0, 0); return; }
    if (WiFi.status() != WL_CONNECTED) { drawHint(fb_, "No WiFi", ""); fb_.pushSprite(0, 0); return; }
    if (!s.valid) { drawHint(fb_, "Connecting", ip); fb_.pushSprite(0, 0); return; }

    const uint8_t cat = active_cat_;
    setPal565(fb_, kAccent, kAccent565[cat]);   // 本卡强调色

    drawClock(fb_, s);
    drawTitle(fb_, kCatTitle[cat]);
    if (rotate_) drawDots(fb_, enabled_mask_ & s.av, cat);

    if (!(s.av & (1u << cat))) {                 // 该分类无数据（多见单显 pin 到不可用类）
        fb_.setTextColor(kDim); fb_.setTextDatum(middle_center);
        fb_.setFont(&fonts::FreeSans12pt7b); fb_.drawString("no data", kCx, kBigY);
    } else {
        switch (cat) {
            case CAT_CPU:  drawCpu(fb_, s);  break;
            case CAT_DISK: drawDisk(fb_, s); break;
            case CAT_GPU:  drawGpu(fb_, s);  break;
            case CAT_HOST: drawHost(fb_, s); break;
            case CAT_NET:  drawNet(fb_, s);  break;
            case CAT_TRAF: drawTraf(fb_, s); break;
        }
    }
    fb_.pushSprite(0, 0);
}

void PcMonitor::onEnter() {
    syncCfg();
    g_net_peak = 1.0f;

    if (!fb_ready_) {
        fb_.setPsram(false);          // 无 PSRAM，显式落内部 SRAM（28KB）
        fb_.setColorDepth(4);         // palette_4bit → createSprite 自动建 16 槽调色板
        fb_ready_ = (fb_.createSprite(240, 240) != nullptr);
        if (fb_ready_) {
            applyPalette();
            Serial.printf("[pc_monitor] fb 4bpp ok, freeheap=%u\n", ESP.getFreeHeap());
        } else {
            Serial.println("[pc_monitor] fb createSprite FAILED -> direct fallback");
        }
    } else {
        applyPalette();
    }

    if (!g_task) xTaskCreate(pollTask, "pcmon", 8192, nullptr, 1, &g_task);  // 懒建一次，常驻
    g_active   = true;
    g_poll_now = true;

    pickInitialCard();
    last_switch_ms_ = millis();
    last_seq_ = 0;
    if (fb_ready_) {
        taskENTER_CRITICAL(&g_mux);
        last_seq_ = g_snap.seq;
        taskEXIT_CRITICAL(&g_mux);
        redraw();
    } else {
        M5Dial.Display.fillScreen(kBgDark);
    }
}

void PcMonitor::onExit() {
    g_active = false;  // 暂停轮询；task 空转不删（fb_ 长驻不释放，同 face_show 策略）
}

void PcMonitor::tick(uint32_t now_ms) {
    if (!fb_ready_) return;
    uint32_t seq;
    taskENTER_CRITICAL(&g_mux);
    seq = g_snap.seq;
    taskEXIT_CRITICAL(&g_mux);

    bool changed = false;
    if (seq != last_seq_) { last_seq_ = seq; changed = true; }            // 数据刷新
    if (rotate_ && (now_ms - last_switch_ms_) >= interval_ms_) {          // 轮询切卡
        advanceCard();
        last_switch_ms_ = now_ms;
        changed = true;
    }
    if (changed) redraw();
}

void PcMonitor::applyState(const state::SharedState& /*s*/) {
    // 监控 cfg（rotate/interval/single/cats）变化 → 重新同步 + 重置轮询 + 重绘。
    // 本 mode 深底固定，不再跟随 g_state.bg_color。
    syncCfg();
    if (!fb_ready_) return;
    pickInitialCard();
    last_switch_ms_ = millis();
    redraw();
}

}  // namespace mochi
