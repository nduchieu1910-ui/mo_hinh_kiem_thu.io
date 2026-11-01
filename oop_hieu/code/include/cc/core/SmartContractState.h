#pragma once
// =====================================================================
//  SmartContractState — bản sao trạng thái (storage) của một hợp đồng
//  Khớp bảng contracts + contract_storage.
// =====================================================================
#include "cc/core/Types.h"

#include <optional>
#include <string>

namespace cc {

struct SmartContractState {
    Address     contractAddress;
    std::string chain;
    std::string name;       // ví dụ "TokenA", "LockBridge"
    KeyValue    storage;    // slot -> giá trị, ví dụ "balances[0xA11CE]" -> "1000"

    std::optional<std::string> get(const std::string& slot) const {
        auto it = storage.find(slot);
        if (it == storage.end()) return std::nullopt;
        return it->second;
    }

    void set(const std::string& slot, std::string value) {
        storage[slot] = std::move(value);
    }
};

} // namespace cc
