#pragma once
// =====================================================================
//  HashProofValidator — bộ kiểm con dấu đơn giản (một người ký).
//    - con dấu = chữ ký của "relayer" trên messageHash
//    - messageHash phải khớp nội dung thư (phát hiện thư bị sửa)
//    - mỗi thư chỉ được thực thi một lần (chống gửi lặp)
//  Bản chặt chẽ hơn (nhiều người ký): MultiSigProofValidator.
// =====================================================================
#include "cc/bridge/CrossChainBridge.h"
#include "cc/core/HashUtil.h"

#include <set>

namespace cc {

class HashProofValidator : public IProofValidator {
public:
    explicit HashProofValidator(Address relayer = "relayer") : relayer_(std::move(relayer)) {}

    std::string createProof(const CrossChainMessage& msg) const override {
        return simSign(relayer_, msg.messageHash);
    }

    std::string validate(const CrossChainMessage& msg) override {
        if (msg.messageHash != msg.computeHash()) return "nội dung thư đã bị sửa";
        if (msg.proof != createProof(msg))        return "con dấu không hợp lệ";
        if (used_.count(msg.messageHash))         return "thư đã được xử lý (replay)";
        return "";
    }

    void markProcessed(const CrossChainMessage& msg) override { used_.insert(msg.messageHash); }

private:
    Address        relayer_;
    std::set<Hash> used_;
};

} // namespace cc
