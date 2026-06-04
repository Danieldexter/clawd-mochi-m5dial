#pragma once

#include <cstdint>
#include <cstddef>

#include "../config.h"  // kRemindersMax / kReminderMsgLen

// Phase 13：定时提醒服务 —— ≤5 条 HH:MM 提醒，NVS 持久化，经墙钟时间（NTP）触发。
// 时钟来源：main.cpp 在 STA 连上后 configTime；本服务用 getLocalTime 比较，未同步则不触发。
// 触发只「暂存」一个 fired 事件；切到 reminder_overlay / 绘屏由 loop 消费后执行（§1.6）。
//
// 并发说明：add/removeAt 由 WS handler（AsyncTCP 上下文）调，tick/begin 由 loop 调，
// 二者都触 g_list —— 沿用本仓库 cc_projects 的同款「无锁、容忍极小概率竞态」约定
// （数组极小、写为罕见用户操作、tick 1Hz，最坏一帧读到半更新消息，自愈）。
namespace mochi::reminder {

struct Reminder {
    uint8_t hour   = 0;
    uint8_t minute = 0;
    bool    daily  = false;   // true=每天重复；false=触发一次后从列表删除
    char    msg[config::kReminderMsgLen] = {0};
};

// 载入 NVS 提醒表（setup() 调一次）。
void begin();

// 主循环调：限频比较墙钟时间，到点暂存 fired 事件 + 维护 daily / 一次性状态。
void tick(uint32_t now_ms);

// 主循环消费暂存的 fired 事件。有则填 msg_out + 清标志 + 返回 true。
bool consumeFired(char* msg_out, size_t cap);

// CRUD（WS handler 调）。add 满 kRemindersMax 或参数非法返回 false；二者写回 NVS。
bool add(uint8_t hour, uint8_t minute, bool daily, const char* msg);
bool removeAt(uint8_t index);

uint8_t         count();
const Reminder* get(uint8_t index);   // 越界返回 nullptr

}  // namespace mochi::reminder
