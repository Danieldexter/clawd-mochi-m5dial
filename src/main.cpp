// clawd-mochi-m5dial — 入口点
// M5Dial.begin() 内部处理 G46 HOLD pin、Display、Touch、Power、I2C。详见 CLAUDE.md §9。

#include <M5Dial.h>

#include "mode_manager.h"
#include "state.h"
#include "modes/eyes_normal.h"
#include "modes/eyes_squish.h"
#include "modes/claude_code.h"
#include "modes/canvas.h"
#include "web/ap_server.h"

static mochi::EyesNormal  g_eyes_normal;
static mochi::EyesSquish  g_eyes_squish;
static mochi::ClaudeCode  g_claude_code;
static mochi::Canvas      g_canvas;

void setup() {
    auto cfg = M5.config();
    M5Dial.begin(cfg, /*enableEncoder=*/true, /*enableRFID=*/false);
    M5Dial.Display.setBrightness(
        mochi::state::g_state.backlight ? mochi::state::kBacklightOnLevel
                                        : mochi::state::kBacklightOffLevel);

    mochi::g_mochi.registerMode(&g_eyes_normal);
    mochi::g_mochi.registerMode(&g_eyes_squish);
    mochi::g_mochi.registerMode(&g_claude_code);
    mochi::g_mochi.registerMode(&g_canvas);
    mochi::g_mochi.setMode(mochi::state::g_state.current_mode);  // 默认 NORMAL_EYES

    mochi::web::WebStack::begin();

    Serial.println("clawd-mochi-m5dial v0.1.0 boot");
}

void loop() {
    M5Dial.update();
    const uint32_t now = millis();
    mochi::g_mochi.tick(now);
    mochi::web::WebStack::tick(now);
    delay(10);
}
