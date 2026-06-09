#include "gif_store.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <cstdio>
#include <cstring>

namespace mochi::gif_store {

namespace {

constexpr const char* kDir     = "/gifs";
constexpr const char* kTmpPath = "/gifs/_tmp.gif";

File   s_up;            // 上传中的 temp 句柄
size_t s_up_bytes = 0;  // 已写字节（超 kMaxBytes 中止）
int    s_up_slot  = -1; // 目标槽（uploadBegin 选定）

}  // namespace

void begin() {
    if (!LittleFS.begin(/*format_on_fail=*/true)) {
        Serial.println("[gif_store] LittleFS mount FAILED");
        return;
    }
    if (!LittleFS.exists(kDir)) LittleFS.mkdir(kDir);
    LittleFS.remove(kTmpPath);   // 清掉上次未完成的残留 temp
}

const char* pathFor(int index, char* buf, size_t cap) {
    snprintf(buf, cap, "/gifs/%d.gif", index);
    return buf;
}

bool exists(int index) {
    if (index < 0 || index >= kMaxSlots) return false;
    char p[24];
    return LittleFS.exists(pathFor(index, p, sizeof(p)));
}

size_t sizeAt(int index) {
    if (!exists(index)) return 0;
    char p[24];
    File f = LittleFS.open(pathFor(index, p, sizeof(p)), "r");
    if (!f) return 0;
    const size_t s = f.size();
    f.close();
    return s;
}

int count() {
    int c = 0;
    for (int i = 0; i < kMaxSlots; ++i) if (exists(i)) ++c;  // 压缩保证连续
    return c;
}

int freeSlot() {
    const int c = count();
    return (c < kMaxSlots) ? c : -1;
}

bool removeAt(int index) {
    if (!exists(index)) return false;
    char p[24];
    LittleFS.remove(pathFor(index, p, sizeof(p)));
    // 压缩：高位槽逐个前移，保持槽号连续（gif_player 可 0..count-1 顺序轮询）
    for (int j = index + 1; j < kMaxSlots; ++j) {
        char src[24], dst[24];
        pathFor(j, src, sizeof(src));
        if (!LittleFS.exists(src)) break;
        pathFor(j - 1, dst, sizeof(dst));
        LittleFS.rename(src, dst);
    }
    return true;
}

bool uploadBegin() {
    uploadAbort();                 // 兜底清旧 temp
    s_up_slot = freeSlot();
    if (s_up_slot < 0) { Serial.println("[gif_store] gallery full (max 4)"); return false; }
    s_up = LittleFS.open(kTmpPath, "w");
    if (!s_up) { Serial.println("[gif_store] open temp FAILED"); s_up_slot = -1; return false; }
    s_up_bytes = 0;
    return true;
}

bool uploadChunk(const uint8_t* data, size_t len) {
    if (!s_up) return false;
    if (s_up_bytes + len > kMaxBytes) {   // 超上限：中止（防写满 Flash）
        Serial.println("[gif_store] upload exceeds 512KB, aborted");
        uploadAbort();
        return false;
    }
    s_up.write(data, len);
    s_up_bytes += len;
    return true;
}

int uploadFinish() {
    if (!s_up) return -1;
    const int slot = s_up_slot;
    s_up.close();
    // 校验 GIF 头（"GIF87a"/"GIF89a" 均以 "GIF8" 开头）
    File f = LittleFS.open(kTmpPath, "r");
    char hdr[4] = {0};
    if (f) { f.read(reinterpret_cast<uint8_t*>(hdr), 4); f.close(); }
    if (strncmp(hdr, "GIF8", 4) != 0 || slot < 0) {
        Serial.println("[gif_store] not a GIF, rejected");
        LittleFS.remove(kTmpPath);
        s_up_slot = -1;
        return -1;
    }
    char dst[24];
    pathFor(slot, dst, sizeof(dst));
    if (LittleFS.exists(dst)) LittleFS.remove(dst);
    const bool ok = LittleFS.rename(kTmpPath, dst);
    s_up_slot = -1;
    Serial.printf("[gif_store] upload -> slot %d (%u bytes) %s\n",
                  slot, static_cast<unsigned>(s_up_bytes), ok ? "ok" : "RENAME FAIL");
    return ok ? slot : -1;
}

void uploadAbort() {
    if (s_up) s_up.close();
    LittleFS.remove(kTmpPath);
    s_up_slot  = -1;
    s_up_bytes = 0;
}

}  // namespace mochi::gif_store
