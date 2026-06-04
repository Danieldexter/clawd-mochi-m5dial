#include "reminder_overlay.h"

#include "../state.h"
#include "../config.h"

#include <M5Dial.h>
#include <cstring>

namespace mochi {

namespace {

// UTF-8 起始字节 → 该字符字节数（覆盖中英混排）。
inline int utf8Len(uint8_t c) {
    if (c < 0x80)        return 1;
    if ((c >> 5) == 0x6) return 2;
    if ((c >> 4) == 0xE) return 3;
    if ((c >> 3) == 0x1E) return 4;
    return 1;
}

// 贪心按像素宽换行到 lines[]（UTF-8 逐字符，CJK 任意处断、英文短句够用）。返回行数（≤maxLines）。
// 模板化显示句柄：M5Dial.Display 的具体 LGFX 类型不必显式命名（同 provisioning 的 auto& 套路）。
template <typename Disp>
int wrapText(Disp& d, const char* s, int16_t maxW, String* lines, int maxLines) {
    int n = 0;
    String cur;
    int i = 0;
    while (s[i] && n < maxLines) {
        const int clen = utf8Len(static_cast<uint8_t>(s[i]));
        String ch;
        for (int k = 0; k < clen && s[i + k]; ++k) ch += s[i + k];
        if (cur.length() > 0 && d.textWidth((cur + ch).c_str()) > maxW) {
            lines[n++] = cur;
            cur = ch;
        } else {
            cur += ch;
        }
        i += clen;
    }
    if (n < maxLines && cur.length() > 0) lines[n++] = cur;
    return n;
}

constexpr int16_t kBarX0 = 45, kBarY = 198, kBarFullW = 150, kBarH = 4;  // 安全圈内（y198 ⇒ |x-120|≤77）

}  // namespace

void ReminderOverlay::show(const char* msg, ModeId return_mode, uint32_t now_ms) {
    snprintf(msg_, sizeof(msg_), "%s", msg ? msg : "");
    return_mode_ = return_mode;
    start_ms_    = now_ms;
    last_bar_w_  = -1;
}

bool ReminderOverlay::finished(uint32_t now_ms) const {
    return (now_ms - start_ms_) >= config::kReminderShowMs;
}

void ReminderOverlay::draw(uint32_t now_ms) {
    auto& d = M5Dial.Display;
    const uint16_t bg = state::g_state.bg_color_565;
    d.fillScreen(bg);

    // 顶部琥珀 "!"（呼应 claude_status waiting 的告知语义，硬方块，无圆角）
    const uint16_t amber = 0xFC60;
    d.fillRect(116, 30, 8, 22, amber);
    d.fillRect(116, 58, 8, 8, amber);

    // 消息：efontCN 支持中英，直绘 16bpp 用不透明文字（白底橙红高对比）
    d.setFont(&fonts::efontCN_24);
    d.setTextColor(TFT_WHITE, bg);
    d.setTextDatum(middle_center);
    String lines[4];
    const int nl = wrapText(d, msg_, 196, lines, 4);
    constexpr int lh = 28;
    int y0 = 120 - (nl - 1) * lh / 2;
    if (y0 < 84) y0 = 84;  // 不压到顶部 "!"
    for (int i = 0; i < nl; ++i) d.drawString(lines[i].c_str(), 120, y0 + i * lh);

    last_bar_w_ = -1;
    drawBar(now_ms);
}

void ReminderOverlay::drawBar(uint32_t now_ms) {
    const uint32_t elapsed = now_ms - start_ms_;
    int16_t bw = 0;
    if (elapsed < config::kReminderShowMs) {
        bw = static_cast<int16_t>(kBarFullW -
             static_cast<int32_t>(kBarFullW) * elapsed / config::kReminderShowMs);
    }
    if (bw < 0) bw = 0;
    if (bw == last_bar_w_) return;
    last_bar_w_ = bw;

    auto& d = M5Dial.Display;
    const uint16_t bg = state::g_state.bg_color_565;
    d.fillRect(kBarX0, kBarY, kBarFullW, kBarH, bg);                 // 擦整条
    if (bw > 0) d.fillRect(120 - bw / 2, kBarY, bw, kBarH, TFT_WHITE);  // 居中剩余（两端收缩）
}

void ReminderOverlay::onEnter() { draw(start_ms_); }  // 入场即满条（now==start）
void ReminderOverlay::onExit()  {}                    // 直绘无缓冲；返回的 mode 自身 onEnter 重绘

void ReminderOverlay::tick(uint32_t now_ms) { drawBar(now_ms); }  // 仅更新倒计时条

}  // namespace mochi
