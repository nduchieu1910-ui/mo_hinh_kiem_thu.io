#pragma once
// =====================================================================
//  CrossChainBridge — BRIDGE PATTERN (phía ABSTRACTION)
//
//  Giữ tham chiếu tới 2 IBlockchainAdapter (Source Chain & Target Chain):
//    - Abstraction : CrossChainBridge  -> TokenBridge, ...
//    - Implementor : IBlockchainAdapter -> EVMAdapter, SolanaAdapter, UtxoAdapter, MockChainAdapter
//  Hai nhánh phát triển độc lập: thêm chain mới = thêm Adapter,
//  thêm nghiệp vụ mới = thêm lớp con của CrossChainBridge.
//
//  Kịch bản trọn gói: lockAndRelay() = Lock ở Chain A  ->  Mint/Exec ở Chain B.
// =====================================================================
#include "cc/adapter/IBlockchainAdapter.h"
#include "cc/core/CrossChainMessage.h"
#include "cc/core/TransactionResult.h"
#include "cc/core/TxPayload.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace cc {

// Tạo và kiểm "con dấu" cho thư xuyên chuỗi, chống gửi lặp.
class IProofValidator {
public:
    virtual ~IProofValidator() = default;

    // Trả về chuỗi rỗng nếu hợp lệ, ngược lại là lý do từ chối.
    virtual std::string validate(const CrossChainMessage& msg) = 0;

    // Con dấu mà chain nguồn đóng lên thư (giả lập).
    virtual std::string createProof(const CrossChainMessage& msg) const { return "SIGNED:" + msg.messageHash; }

    // Gọi SAU KHI chain đích thực thi thành công: ghi nhớ thư để chặn gửi lặp.
    // Tách khỏi validate() để thư thất bại ở chain đích vẫn được thử lại.
    virtual void markProcessed(const CrossChainMessage&) {}
};

// Kết quả của kịch bản trọn gói Lock -> Mint.
struct BridgeResult {
    TransactionResult lock;      // giao dịch khóa ở chain nguồn
    CrossChainMessage message;   // lá thư được tạo từ sự kiện "Locked"
    TransactionResult target;    // giao dịch mint/exec ở chain đích
    bool ok() const { return lock.ok() && target.ok(); }
};

class CrossChainBridge {
public:
    CrossChainBridge(std::shared_ptr<IBlockchainAdapter> source,
                     std::shared_ptr<IBlockchainAdapter> target,
                     std::shared_ptr<IProofValidator>    validator)
        : source_(std::move(source)),
          target_(std::move(target)),
          validator_(std::move(validator)) {
        if (!source_ || !target_ || !validator_)
            throw std::invalid_argument("CrossChainBridge: thiếu adapter hoặc validator");
    }

    virtual ~CrossChainBridge() = default;

    // Kịch bản trọn gói:
    //   1. Lock   : gửi lệnh "lock" qua adapter chain nguồn
    //   2. Thư    : đọc sự kiện "Locked" -> dựng CrossChainMessage + con dấu
    //   3. Mint   : relay(thư) -> adapter chain đích
    BridgeResult lockAndRelay(const Address& sender, const Address& receiver, Amount amount,
                              KeyValue extra = {}) {
        BridgeResult out;
        out.message.sourceChain = source_->chainName();
        out.message.targetChain = target_->chainName();
        out.message.sender = sender;
        out.message.receiver = receiver;
        out.message.amount = amount;
        out.message.payload = extra;

        TxPayload lockReq{"lock", sender, "", amount, extra};
        lockReq.args["receiver"] = receiver;
        lockReq.args["targetChain"] = target_->chainName();
        out.lock = source_->sendTransaction(lockReq);
        if (!out.lock.ok()) {
            out.message.status = MessageStatus::FAILED;
            out.target = TransactionResult::failure(TxStatus::FAILED,
                "khóa ở chain nguồn thất bại: " + out.lock.revertReason, target_->chainName());
            return out;
        }

        const Event* locked = nullptr;
        for (const Event& e : out.lock.events)
            if (e.name == "Locked") { locked = &e; break; }
        if (!locked) {
            out.message.status = MessageStatus::FAILED;
            out.target = TransactionResult::failure(TxStatus::FAILED, "không thấy sự kiện Locked", target_->chainName());
            return out;
        }
        auto nonceIt = locked->fields.find("nonce");
        out.message.nonce = nonceIt == locked->fields.end() ? 0 : std::stoull(nonceIt->second);
        out.message.messageHash = out.message.computeHash();
        out.message.proof = validator_->createProof(out.message);

        out.target = relay(out.message);
        return out;
    }

    // Chuyển một lá thư đã có sang chain đích (luồng cố định cho mọi bridge):
    //   kiểm thư -> dựng yêu cầu cho chain đích -> gửi qua adapter đích -> cập nhật trạng thái thư
    TransactionResult relay(CrossChainMessage& msg) {
        msg.status = MessageStatus::RELAYED;

        if (std::string reason = verify(msg); !reason.empty()) {
            msg.status = MessageStatus::FAILED;
            return TransactionResult::failure(TxStatus::FAILED, reason, target_->chainName());
        }
        msg.status = MessageStatus::VERIFIED;

        TransactionResult result = target_->sendTransaction(buildTargetPayload(msg));
        if (result.ok()) validator_->markProcessed(msg);
        msg.status = result.ok() ? MessageStatus::EXECUTED : MessageStatus::FAILED;
        return result;
    }

    // Kiểm thư mà không gửi: đúng chain + con dấu hợp lệ. Rỗng = hợp lệ.
    std::string verify(const CrossChainMessage& msg) const {
        if (msg.sourceChain != source_->chainName())
            return "thư không đến từ chain nguồn của bridge (" + msg.sourceChain + ")";
        if (msg.targetChain != target_->chainName())
            return "thư không gửi tới chain đích của bridge (" + msg.targetChain + ")";
        return validator_->validate(msg);
    }

    // Con dấu cho một thư (Relayer dùng sau khi dựng thư từ sự kiện).
    std::string proofFor(const CrossChainMessage& msg) const { return validator_->createProof(msg); }

    const IBlockchainAdapter& source() const { return *source_; }
    const IBlockchainAdapter& target() const { return *target_; }

protected:
    // Mỗi nghiệp vụ tự quyết định chain đích phải làm gì với thư.
    virtual TxPayload buildTargetPayload(const CrossChainMessage& msg) const = 0;

    std::shared_ptr<IBlockchainAdapter> source_;
    std::shared_ptr<IBlockchainAdapter> target_;
    std::shared_ptr<IProofValidator>    validator_;
};

// Nghiệp vụ chuyển token: khóa ở chain nguồn, phát (mint) ở chain đích.
class TokenBridge : public CrossChainBridge {
public:
    using CrossChainBridge::CrossChainBridge;

protected:
    TxPayload buildTargetPayload(const CrossChainMessage& msg) const override {
        TxPayload p;
        p.action = "mint";
        p.to = msg.receiver;
        p.amount = msg.amount;
        p.args = msg.payload;
        p.args["messageHash"] = msg.messageHash;
        return p;
    }
};

} // namespace cc
