#pragma once

#include <cstdint>
#include "../modes/i_mode.h"

// 前置声明：ccStatusToName/ccStyleToName 按值收枚举（固定底层类型，前置声明足够），避免在头里全量 include state.h
namespace mochi::state { enum class CcStatus : uint8_t; enum class CcStyle : uint8_t; }

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

// Phase 10：Settings 页（正常 STA 模式，WS 原生）
constexpr const char* kTypeSetPcIp       = "set_pc_ip";     // 字段 ip："192.168.x.x"
constexpr const char* kTypeWifiReset     = "wifi_reset";    // 清 NVS 凭据 → 重启进配网
constexpr const char* kTypeRestart       = "restart";       // 软重启
constexpr const char* kTypeSetCcStyle    = "set_cc_style";  // Phase 11：联动模式风格（字段 style："clawd"|"sparkle"）
constexpr const char* kTypeSetCcScope    = "set_cc_scope";  // Phase 11b：联动作用域（字段 scope：项目 CLAUDE_PROJECT_DIR，空/缺省=全局）
constexpr const char* kTypeSetFace       = "set_face";      // Phase 12：face_show 表情选择（字段 key："face_wuyu"/"anim_smile"...）
constexpr const char* kTypeReminderAdd   = "reminder_add";  // Phase 13：新增提醒（字段 hour/minute/daily/msg）
constexpr const char* kTypeReminderDel   = "reminder_del";  // Phase 13：删除提醒（字段 index）
constexpr const char* kTypeSetMonitor    = "set_monitor";   // Phase 14b：PC Monitor 面板配置（字段 rotate/interval/single/cats[]）

// === Device → Client message types ===
constexpr const char* kTypeState         = "state";

// === Mode id ↔ string 双向映射 ===
struct ModeMapping {
    ModeId      id;
    const char* name;
};
constexpr ModeMapping kModeMappings[] = {
    {ModeId::FACE_SHOW,     "face_show"},      // v0.3.0：统一表情 mode（合并原 Normal/Squish Eyes）
    {ModeId::CLAUDE_CODE,   "claude_code"},
    {ModeId::CANVAS,        "canvas"},
    {ModeId::CLAUDE_STATUS, "claude_status"},  // Phase 11：Claude Code 联动模式
    {ModeId::PC_MONITOR,    "pc_monitor"},     // Phase 14：PC 监控面板
    // REMINDER_OVERLAY 故意不映射：瞬态 overlay 永不进 g_state.current_mode（见 main.cpp 编排）
};

const char* modeIdToName(ModeId id);
bool        nameToModeId(const char* name, ModeId& out);

// === PC Monitor 分类（Phase 14b）===
// 索引即位序：state::MonitorCfg.single_cat / enabled_mask 用此索引；也是轮询顺序与底部圆点顺序。
// 与 pc_monitor.cpp 的卡片渲染顺序、stats.json 子对象键保持一致（顺序无关，按名查）。
constexpr uint8_t     kMonCatCount = 6;
constexpr const char* kMonCatNames[kMonCatCount] = {"cpu", "disk", "gpu", "host", "net", "traf"};
const char* monCatName(uint8_t idx);                 // 越界 → ""
bool        monCatIndex(const char* name, uint8_t& out);

// Phase 11：CcStatus → 字符串（"idle"/"working"/"waiting"），供 state 广播给 Web 面板展示
const char* ccStatusToName(state::CcStatus s);

// Phase 11：CcStyle ↔ 字符串（"clawd"/"sparkle"），Web 风格选择器 + NVS 持久化用
const char* ccStyleToName(state::CcStyle s);
bool        nameToCcStyle(const char* name, state::CcStyle& out);

// === HEX 字符串 ↔ RGB565 双向 ===
uint16_t hexToRgb565(const char* hex);
void     rgb565ToHex(uint16_t rgb, char out[8]);  // "#RRGGBB" + null

}  // namespace mochi::web::protocol
