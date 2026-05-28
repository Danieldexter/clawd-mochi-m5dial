#pragma once

#include <cstdint>
#include "../modes/i_mode.h"

namespace mochi::web::protocol {

// === Client → Device message types ===
constexpr const char* kTypeSetMode       = "set_mode";
constexpr const char* kTypeSetSpeed      = "set_speed";
constexpr const char* kTypeSetBgColor    = "set_bg_color";
constexpr const char* kTypeSetPenColor   = "set_pen_color";
constexpr const char* kTypeSetBacklight  = "set_backlight";
constexpr const char* kTypeClearCanvas   = "clear_canvas";
constexpr const char* kTypeTerminalInput = "terminal_input";
constexpr const char* kTypeStroke        = "stroke";        // Phase 7：Web 画 → 设备显示

// === Device → Client message types ===
constexpr const char* kTypeState         = "state";

// === Mode id ↔ string 双向映射 ===
struct ModeMapping {
    ModeId      id;
    const char* name;
};
constexpr ModeMapping kModeMappings[] = {
    {ModeId::NORMAL_EYES, "normal_eyes"},
    {ModeId::SQUISH_EYES, "squish_eyes"},
    {ModeId::CLAUDE_CODE, "claude_code"},
    {ModeId::CANVAS,      "canvas"},
};

const char* modeIdToName(ModeId id);
bool        nameToModeId(const char* name, ModeId& out);

// === HEX 字符串 ↔ RGB565 双向 ===
uint16_t hexToRgb565(const char* hex);
void     rgb565ToHex(uint16_t rgb, char out[8]);  // "#RRGGBB" + null

}  // namespace mochi::web::protocol
