#pragma once

#include <cstddef>
#include <cstdint>

// v0.4.0：GIF 图库存储（LittleFS /gifs/<i>.gif，i=0..kMaxSlots-1，槽号连续——删除即压缩前移）。
// 设备端读 + 槽管理 + Web 上传写入（分块）。仿 services/reminder 风格。
// LittleFS 挂载由本服务 begin() 负责（幂等）：原挂载在 WebStack::begin，离线启动不跑 web → 不挂 FS，
// 故这里独立挂一次，保证无 WiFi 也能播 GIF。
namespace mochi::gif_store {

constexpr int    kMaxSlots = 4;
constexpr size_t kMaxBytes = 512u * 1024u;   // 单张上限 512KB（保护 Flash）

void begin();                                 // 挂 LittleFS（幂等）+ 确保 /gifs 目录

int    count();                               // 现有槽数（0..kMaxSlots，连续）
bool   exists(int index);
size_t sizeAt(int index);                     // 字节数（0 若无）
// 写 "/gifs/<index>.gif" 到 buf，返回 buf。
const char* pathFor(int index, char* buf, size_t cap);

int  freeSlot();                              // 首个空槽（=count()），满则 -1
bool removeAt(int index);                     // 删除并压缩前移高位槽（保持连续）

// 上传写入（HTTP onUpload 分块）。temp → finish 校验 GIF 头 + rename 到 freeSlot。
bool uploadBegin();                           // 选 freeSlot 打开 temp；满/失败 → false
bool uploadChunk(const uint8_t* data, size_t len);  // 追加；超 kMaxBytes → 中止 + false
int  uploadFinish();                          // 校验 + rename；成功返回目标槽 index，失败 -1
void uploadAbort();                           // 删 temp（连接中断 / 校验失败兜底）

}  // namespace mochi::gif_store
