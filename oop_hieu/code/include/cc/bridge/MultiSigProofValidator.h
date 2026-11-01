#pragma once
// =====================================================================
//  MultiSigProofValidator — PROOF VALIDATION GIẢ LẬP kiểu "hội đồng xác nhận".
//
//  Mô phỏng cách nhiều bridge thật hoạt động: có N validator theo dõi chain
//  nguồn; khi thấy sự kiện "Locked", mỗi validator ký lên mã băm của thư.
//  Chain đích chỉ chấp nhận thư khi có ít nhất M chữ ký hợp lệ (M-of-N).
//
//  Định dạng con dấu: "val_a=<chữ ký>;val_b=<chữ ký>;..."
//
//  Thư bị từ chối khi:
//    1. mã băm không khớp nội dung (thư bị sửa: số tiền, người nhận...)
//    2. không đủ M chữ ký hợp lệ (người ký lạ / chữ ký sai không được tính,
//       một người ký hai lần chỉ tính một)
//    3. thư đã được thực thi (cùng mã băm, hoặc cùng chain nguồn + nonce)
// =====================================================================
#include "cc/bridge/CrossChainBridge.h"
#include "cc/core/HashUtil.h"

#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cc {

class MultiSigProofValidator : public IProofValidator {
public:
    MultiSigProofValidator(std::vector<Address> validators, std::size_t threshold)
        : validators_(validators.begin(), validators.end()),
          attestSigners_(std::move(validators)),
          threshold_(threshold) {
        if (threshold_ == 0 || threshold_ > validators_.size())
            throw std::invalid_argument("MultiSigProofValidator: ngưỡng phải từ 1 đến số validator");
    }

    // Con dấu do các validator ký (mặc định: tất cả validator cùng ký).
    std::string createProof(const CrossChainMessage& msg) const override {
        return attest(msg, attestSigners_);
    }

    // Ký bởi một nhóm người tùy chọn (dùng trong test để giả lập thiếu chữ ký / người lạ).
    static std::string attest(const CrossChainMessage& msg, const std::vector<Address>& signers) {
        std::string proof;
        for (const Address& s : signers) {
            if (!proof.empty()) proof += ";";
            proof += s + "=" + simSign(s, msg.messageHash);
        }
        return proof;
    }

    // Giả lập một số validator ngừng hoạt động: chỉ những người này còn ký.
    void setActiveSigners(std::vector<Address> signers) { attestSigners_ = std::move(signers); }

    std::string validate(const CrossChainMessage& msg) override {
        if (msg.messageHash != msg.computeHash()) return "nội dung thư đã bị sửa (mã băm không khớp)";

        std::set<Address> valid;
        for (const auto& [signer, sig] : parse(msg.proof))
            if (validators_.count(signer) && sig == simSign(signer, msg.messageHash)) valid.insert(signer);
        if (valid.size() < threshold_)
            return "không đủ chữ ký hợp lệ: " + std::to_string(valid.size()) + "/" + std::to_string(threshold_);

        if (processedHashes_.count(msg.messageHash) ||
            processedNonces_.count({msg.sourceChain, msg.nonce}))
            return "thư đã được xử lý (replay)";
        return "";
    }

    void markProcessed(const CrossChainMessage& msg) override {
        processedHashes_.insert(msg.messageHash);
        processedNonces_.insert({msg.sourceChain, msg.nonce});
    }

    std::size_t threshold() const { return threshold_; }

    // Memento: những gì validator "nhớ" (Sandbox chụp / khôi phục).
    // Nếu không khôi phục phần này, chạy lại một kịch bản sẽ bị chặn nhầm là replay.
    struct State {
        std::vector<Address>                            attestSigners;
        std::set<Hash>                                  processedHashes;
        std::set<std::pair<std::string, std::uint64_t>> processedNonces;
    };
    State saveState() const { return {attestSigners_, processedHashes_, processedNonces_}; }
    void restoreState(const State& s) {
        attestSigners_ = s.attestSigners;
        processedHashes_ = s.processedHashes;
        processedNonces_ = s.processedNonces;
    }

private:
    static std::vector<std::pair<Address, std::string>> parse(const std::string& proof) {
        std::vector<std::pair<Address, std::string>> out;
        std::size_t start = 0;
        while (start < proof.size()) {
            std::size_t end = proof.find(';', start);
            if (end == std::string::npos) end = proof.size();
            std::string part = proof.substr(start, end - start);
            if (auto eq = part.find('='); eq != std::string::npos)
                out.emplace_back(part.substr(0, eq), part.substr(eq + 1));
            start = end + 1;
        }
        return out;
    }

    std::set<Address>                                validators_;
    std::vector<Address>                             attestSigners_;
    std::size_t                                      threshold_;
    std::set<Hash>                                   processedHashes_;
    std::set<std::pair<std::string, std::uint64_t>>  processedNonces_;
};

} // namespace cc
