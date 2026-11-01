#pragma once
// Băm giả lập (đủ cho môi trường test cục bộ, KHÔNG dùng cho bảo mật thật).
#include "cc/core/Types.h"

#include <cstdio>
#include <functional>
#include <string>

namespace cc {

inline Hash simHash(const std::string& raw) {
    // Hai lần băm với "muối" khác nhau -> chuỗi hex 32 ký tự cho dễ đọc.
    std::size_t a = std::hash<std::string>{}(raw);
    std::size_t b = std::hash<std::string>{}(raw + "#salt");
    char buf[40];
    std::snprintf(buf, sizeof buf, "0x%016llx%016llx",
                  static_cast<unsigned long long>(a), static_cast<unsigned long long>(b));
    return buf;
}

// Chữ ký giả lập: "người ký" + "nội dung được ký". ExecutionEngine tính lại
// và so sánh để kiểm chữ ký (mô phỏng, không dùng khóa bí mật thật).
inline std::string simSign(const Address& signer, const std::string& message) {
    return "sig:" + signer + ":" + message;
}

} // namespace cc
