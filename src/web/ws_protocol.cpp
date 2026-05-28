#include "ws_protocol.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace mochi::web::protocol {

const char* modeIdToName(ModeId id) {
    for (const auto& m : kModeMappings) {
        if (m.id == id) return m.name;
    }
    return "unknown";
}

bool nameToModeId(const char* name, ModeId& out) {
    if (!name) return false;
    for (const auto& m : kModeMappings) {
        if (strcmp(name, m.name) == 0) {
            out = m.id;
            return true;
        }
    }
    return false;
}

uint16_t hexToRgb565(const char* hex) {
    if (!hex) return 0;
    if (hex[0] == '#') ++hex;
    if (strlen(hex) < 6) return 0;
    char rs[3] = {hex[0], hex[1], 0};
    char gs[3] = {hex[2], hex[3], 0};
    char bs[3] = {hex[4], hex[5], 0};
    uint8_t r = static_cast<uint8_t>(strtoul(rs, nullptr, 16));
    uint8_t g = static_cast<uint8_t>(strtoul(gs, nullptr, 16));
    uint8_t b = static_cast<uint8_t>(strtoul(bs, nullptr, 16));
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void rgb565ToHex(uint16_t rgb, char out[8]) {
    uint8_t r = static_cast<uint8_t>(((rgb >> 11) & 0x1F) << 3);
    uint8_t g = static_cast<uint8_t>(((rgb >> 5)  & 0x3F) << 2);
    uint8_t b = static_cast<uint8_t>(( rgb        & 0x1F) << 3);
    snprintf(out, 8, "#%02X%02X%02X", r, g, b);
}

}  // namespace mochi::web::protocol
