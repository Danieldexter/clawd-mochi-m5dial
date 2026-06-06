#pragma once

// v0.3.0：蜂鸣器音效（命名 earcon）。封装 M5Dial.Speaker（M5Unified Speaker_Class，G3 蜂鸣器）。
// tone() 后台 task 非阻塞（§1.6）；M5Dial.begin 已初始化 Speaker，这里只设音量 + 提供语义化音效。
// 注意：用 M5Dial.Speaker.tone，**不要**裸 ledc 占 G3（会与 M5Unified 驱动冲突，见 CLAUDE.md §9）。

namespace mochi::audio::beeper {

void begin();          // 设音量（可调）

void tick();           // 编码器每 detent：极短"咔"
void modeSwitch();     // 旋钮单击切 mode：80ms
void penDown();        // Canvas 落笔：20ms"嗒"
void clientConnect();  // WS 新客户端连接：上扬双音
void pinToggle(bool pinned);  // Faces 长按锁定/解锁：两音区分

// Claude 风味彩蛋
void bootChime();      // 开机唤醒：上扬双音
void attention();      // Claude Link → waiting：注意音（会话要你确认）
void success();        // Claude Link → idle：完成音（任务结束）
void yawn();           // 打盹唤醒：下行"哈欠"

}  // namespace mochi::audio::beeper
