#pragma once
// =====================================================================
//  CrossChainMessage — "lá thư" truyền từ chain nguồn sang chain đích
//  Khớp bảng cross_chain_messages trong database.
// =====================================================================
#include "cc/core/Types.h"

#include <functional>
#include <string>

namespace cc {

struct CrossChainMessage {
    Hash          messageHash;   // định danh duy nhất của thư
    std::string   sourceChain;   // tên chain nguồn, ví dụ "ChainA-EVM"
    std::string   targetChain;   // tên chain đích, ví dụ "ChainB-UTXO"
    Address       sender;        // người gửi ở chain nguồn
    Address       receiver;      // người nhận ở chain đích
    std::uint64_t nonce = 0;     // số thứ tự thư, dùng để chống gửi lặp
    Amount        amount = 0;    // số tiền / token được chuyển
    KeyValue      payload;       // dữ liệu thêm (token, hành động...)
    std::string   proof;         // "con dấu" giả lập do chain nguồn ký
    MessageStatus status = MessageStatus::SENT;

    // Băm các trường cố định của thư (giả lập, đủ cho môi trường test).
    Hash computeHash() const {
        std::string raw = sourceChain + "|" + targetChain + "|" + sender + "|" +
                          receiver + "|" + std::to_string(nonce) + "|" +
                          amountToString(amount);
        for (const auto& [k, v] : payload) raw += "|" + k + "=" + v;
        return std::to_string(std::hash<std::string>{}(raw));
    }
};

} // namespace cc
