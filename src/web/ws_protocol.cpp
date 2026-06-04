#include "ws_protocol.h"

#include "../state.h"

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

const char* monCatName(uint8_t idx) {
    return (idx < kMonCatCount) ? kMonCatNames[idx] : "";
}

bool monCatIndex(const char* name, uint8_t& out) {
    if (!name) return false;
    for (uint8_t i = 0; i < kMonCatCount; ++i) {
        if (strcmp(name, kMonCatNames[i]) == 0) { out = i; return true; }
    }
    return false;
}

const char* ccStatusToName(state::CcStatus s) {
    switch (s) {
        case state::CcStatus::WORKING: return "working";
        case state::CcStatus::WAITING: return "waiting";
        default:                       return "idle";
    }
}

const char* ccStyleToName(state::CcStyle s) {
    switch (s) {
        case state::CcStyle::SPARKLE: return "sparkle";
        default:                      return "clawd";
    }
}

bool nameToCcStyle(const char* name, state::CcStyle& out) {
    if (!name) return false;
    if (strcmp(name, "clawd")   == 0) { out = state::CcStyle::CLAWD;   return true; }
    if (strcmp(name, "sparkle") == 0) { out = state::CcStyle::SPARKLE; return true; }
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
