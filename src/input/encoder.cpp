#include "encoder.h"

#include "../config.h"

#include <M5Dial.h>

namespace mochi::input::encoder {

namespace { int32_t last_pos_ = 0; }

void begin() { last_pos_ = M5Dial.Encoder.read(); }

int poll() {
    const int32_t pos  = M5Dial.Encoder.read();
    const int32_t diff = pos - last_pos_;
    const int     det  = static_cast<int>(diff / config::kEncoderCountsPerDetent);  // 向零截断，双向对称
    if (det != 0) last_pos_ += static_cast<int32_t>(det) * config::kEncoderCountsPerDetent;  // 保留余数
    return det;
}

}  // namespace mochi::input::encoder
