# Phase 13 — Reminder 定时提醒 设计说明

> ≤5 条**每日定时 HH:MM** 提醒（用户确认的时间模型），经 **NTP** 墙钟触发，NVS 持久化跨重启。
> 到点切瞬态全屏 overlay，30s（或轻点 BtnA）后回原 mode。
> 代码：`src/services/reminder.{h,cpp}`（服务）+ `src/modes/reminder_overlay.{h,cpp}`（渲染）。

## 1. 数据模型 & NVS

```c++
struct Reminder { uint8_t hour, minute; bool daily; char msg[config::kReminderMsgLen=96]; };
```
- 上限 `config::kRemindersMax = 5`；消息缓冲 96 字节（前端限 20 字符 → 中英 emoji 混排无截断）。
- NVS：自有 `Preferences`，复用 namespace `clawd`，key `"reminders"`，记录换行分隔、字段 `|` 分隔：`"H|M|daily|msg"`（仿 provisioning 的 cc_projects 序列化）。
- `daily=false` 的一次性提醒触发后**从列表删除并写回 NVS**；`daily=true` 每天重复。

## 2. 时间源（NTP）
- `main.cpp` STA 连上后 `configTime(kTzOffsetSec=UTC+8, 0, "pool.ntp.org", "ntp.aliyun.com")`（非阻塞后台同步）。
- 时区**固定 UTC+8**（用户在中国），config 常量，将来要改集中此处。
- `reminder::tick` 用 `getLocalTime(&t, 0)`（无等待）取墙钟；未同步 / `tm_year+1900 < 2024` → **不触发**。
- 自洽性：设提醒必经 web → STA → NTP 全在线；单机（无网）无 web 可设，故无须触发。

## 3. 触发语义（reminder::tick，loop 限频 1s 调）
- 防同分钟重复：每条记 `g_last_fired_key = yday*1440 + hour*60 + min`（不入 NVS）；本分钟已触发则跳过。
- 命中 `hour==tm_hour && minute==tm_min && key 未触发` → 暂存 fired(msg) + 置 key；一次性则 `removeAt`；一次只触发一条；`g_fired` 待消费期间不再触发。

## 4. Overlay 编排（main.cpp loop，仿 cc 事件消费）
- `consumeFired` → `g_reminder_overlay.show(msg, ret, now)` → `setMode(REMINDER_OVERLAY)`。
- **关键技巧**：overlay 是瞬态 mode，**永不写入 `g_state.current_mode`**。因 `buildStateJson` 的 `mode` 取自 `g_state.current_mode`（非 `currentId()`），web 面板照常显示底层 mode；30s 后 `setMode(returnMode)` 回到它。故 REMINDER_OVERLAY **不进 kModeMappings、无 web 按钮**。
- 结束条件：`finished(now)`（≥30s）**或** `M5Dial.BtnA.wasClicked()`（轻点提前消除；与长按 5s 进配网不冲突）。
- **可打断任何 mode（含 canvas）**：canvas sprite 保留不释放（canvas.cpp:89），回切不丢画作；区别于 cc-linkage 被动避让 canvas —— 闹钟是用户显式设定，不应静默吞掉。cc 自动切入加 `cur != REMINDER_OVERLAY` 守卫，不抢占正显示的闹钟。

## 5. Overlay 渲染（直绘，无离屏缓冲；仿 provisioning::drawSetupScreen）
- onEnter `fillScreen(bg)` + 顶部琥珀硬方块 `!` + `efontCN_24`（中英）换行消息（贪心 UTF-8 换行，maxW 196，垂直居中 ≤4 行）+ 底部居中倒计时条。
- tick 仅更新倒计时条（擦条 + 居中收缩），消息静态不重绘 → 无全屏直绘闪烁。

## 6. WS 协议 & UI
- `reminder_add {hour,minute,daily,msg}`（前端 `<input type=time>` 拆 HH/MM）、`reminder_del {index}`；state 广播 `reminders:[{hour,minute,daily,msg}]`。
- UI 在 index.html 主面板常显 section（time + msg + daily + Add + 列表，✕ 删除）；`app.js::renderReminders` 每次 state 重建列表。

## 7. 并发说明
- `add/removeAt` 由 WS handler（AsyncTCP 上下文）调，`tick/begin` 由 loop 调，二者都触 `g_list`。沿用本仓库 cc_projects 的**无锁、容忍极小概率竞态**约定（数组极小、写为罕见用户操作、tick 1Hz；最坏一帧读到半更新消息，自愈）。

## 8. 不改 / scope
- 不做每周 / 工作日 / 间隔重复（仅每日 + 一次性）；不接 RTC（BM8563 列 v0.3.0+，掉电后靠 NTP 重新对时）；标志为 efontCN_24 引入约 +560KB flash（CJK 字库；flash 占用 50.6%，余量充足，若紧首选换小字库）。
