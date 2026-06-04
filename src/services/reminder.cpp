#include "reminder.h"

#include "../config.h"

#include <Arduino.h>
#include <Preferences.h>
#include <time.h>
#include <cstdio>
#include <cstring>

namespace mochi::reminder {

namespace {

Preferences prefs;
constexpr const char* kKeyReminders = "reminders";  // namespace = config::kNvsNamespace

Reminder g_list[config::kRemindersMax];
uint8_t  g_count = 0;

// 防同一分钟内重复触发：记录每条上次触发的「yday*1440 + 分钟序」键（不入 NVS）。
int32_t  g_last_fired_key[config::kRemindersMax];

// fired 暂存（tick 写、loop consumeFired 读；均在 loop 单线程，无需 volatile）。
bool g_fired = false;
char g_fired_msg[config::kReminderMsgLen] = {0};

uint32_t g_last_tick_ms = 0;

// 反序列化：每行 "H|M|daily|msg"
void load() {
    prefs.begin(config::kNvsNamespace, /*readOnly=*/true);
    const String raw = prefs.getString(kKeyReminders, "");
    prefs.end();

    g_count = 0;
    int start = 0;
    while (start < raw.length() && g_count < config::kRemindersMax) {
        int end = raw.indexOf('\n', start);
        if (end < 0) end = raw.length();
        const String line = raw.substring(start, end);
        start = end + 1;
        if (line.length() == 0) continue;

        const int p1 = line.indexOf('|');
        const int p2 = line.indexOf('|', p1 + 1);
        const int p3 = line.indexOf('|', p2 + 1);
        if (p1 < 0 || p2 < 0 || p3 < 0) continue;

        Reminder& r = g_list[g_count];
        r.hour   = static_cast<uint8_t>(line.substring(0, p1).toInt());
        r.minute = static_cast<uint8_t>(line.substring(p1 + 1, p2).toInt());
        r.daily  = line.substring(p2 + 1, p3).toInt() != 0;
        snprintf(r.msg, sizeof(r.msg), "%s", line.substring(p3 + 1).c_str());
        g_last_fired_key[g_count] = -1;
        ++g_count;
    }
}

void save() {
    String out;
    for (uint8_t i = 0; i < g_count; ++i) {
        if (i) out += '\n';
        out += String(g_list[i].hour);   out += '|';
        out += String(g_list[i].minute); out += '|';
        out += (g_list[i].daily ? '1' : '0'); out += '|';
        out += g_list[i].msg;
    }
    prefs.begin(config::kNvsNamespace, /*readOnly=*/false);
    prefs.putString(kKeyReminders, out);  // putString 立即落盘
    prefs.end();
}

}  // namespace

void begin() {
    for (auto& k : g_last_fired_key) k = -1;
    load();
    Serial.printf("[reminder] loaded %u reminders\n", g_count);
}

void tick(uint32_t now_ms) {
    if (g_count == 0 || g_fired) return;            // 无提醒，或上个 fired 待消费
    if (now_ms - g_last_tick_ms < 1000) return;     // 限频 1s
    g_last_tick_ms = now_ms;

    struct tm t;
    if (!getLocalTime(&t, 0)) return;               // NTP 未同步（无等待）→ 不触发
    if (t.tm_year + 1900 < 2024) return;            // 时间显然未同步

    const int32_t key = static_cast<int32_t>(t.tm_yday) * 1440 + t.tm_hour * 60 + t.tm_min;
    for (uint8_t i = 0; i < g_count; ++i) {
        Reminder& r = g_list[i];
        if (r.hour == t.tm_hour && r.minute == t.tm_min && g_last_fired_key[i] != key) {
            g_last_fired_key[i] = key;                       // 本分钟已触发，勿重复
            snprintf(g_fired_msg, sizeof(g_fired_msg), "%s", r.msg);
            g_fired = true;
            if (!r.daily) removeAt(i);                       // 一次性：触发后删除（内部 save）
            break;                                           // 一次只触发一条
        }
    }
}

bool consumeFired(char* msg_out, size_t cap) {
    if (!g_fired) return false;
    if (msg_out && cap) snprintf(msg_out, cap, "%s", g_fired_msg);
    g_fired = false;
    return true;
}

bool add(uint8_t hour, uint8_t minute, bool daily, const char* msg) {
    if (g_count >= config::kRemindersMax) return false;
    if (hour > 23 || minute > 59) return false;

    Reminder& r = g_list[g_count];
    r.hour = hour;
    r.minute = minute;
    r.daily = daily;
    snprintf(r.msg, sizeof(r.msg), "%s", msg ? msg : "");
    g_last_fired_key[g_count] = -1;
    ++g_count;
    save();
    return true;
}

bool removeAt(uint8_t index) {
    if (index >= g_count) return false;
    for (uint8_t i = index; i + 1 < g_count; ++i) {
        g_list[i]           = g_list[i + 1];
        g_last_fired_key[i] = g_last_fired_key[i + 1];
    }
    --g_count;
    save();
    return true;
}

uint8_t count() { return g_count; }

const Reminder* get(uint8_t index) {
    return (index < g_count) ? &g_list[index] : nullptr;
}

}  // namespace mochi::reminder
