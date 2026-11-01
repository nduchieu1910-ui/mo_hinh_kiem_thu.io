#pragma once
// =====================================================================
//  Thêm 2 lớp con cho phía ABSTRACTION của Bridge pattern (Bước 5).
//  Không phải sửa dòng nào ở Adapter / Factory / ExecutionEngine phía dưới.
//
//   CrossChainBridge
//     ├── TokenBridge    : khóa ở A  -> mint ở B           (Bước 1–4)
//     ├── LendingBridge  : thế chấp ở A -> cho vay ở B      (mới)
//     └── UnlockBridge   : sự kiện ở B -> mở khóa ở A       (mới: chiều về / trả thế chấp)
// =====================================================================
#include "cc/bridge/CrossChainBridge.h"

#include <string>

namespace cc {

// Thư từ chain thế chấp: amount = số tiền thế chấp; payload["memo"] = "borrow=<số tiền vay>".
class LendingBridge : public CrossChainBridge {
public:
    using CrossChainBridge::CrossChainBridge;

    static Amount borrowAmountFromMemo(const std::string& memo) {
        const std::string key = "borrow=";
        auto pos = memo.find(key);
        if (pos == std::string::npos) return 0;
        try { return amountFromString(memo.substr(pos + key.size())); } catch (const std::exception&) { return 0; }
    }

protected:
    TxPayload buildTargetPayload(const CrossChainMessage& msg) const override {
        auto memo = msg.payload.find("memo");
        TxPayload p;
        p.action = "borrow";
        p.to = msg.receiver;                                       // người vay trên chain cho vay
        p.amount = borrowAmountFromMemo(memo == msg.payload.end() ? "" : memo->second);
        p.args["collateral"] = amountToString(msg.amount);
        p.args["collateralOwner"] = msg.sender;                    // để trả thế chấp về đúng người
        p.args["messageHash"] = msg.messageHash;
        return p;
    }
};

// Trả tiền đang bị khóa ở chain đích về cho người nhận (chiều về của token, hoặc trả thế chấp).
class UnlockBridge : public CrossChainBridge {
public:
    using CrossChainBridge::CrossChainBridge;

protected:
    TxPayload buildTargetPayload(const CrossChainMessage& msg) const override {
        TxPayload p;
        p.action = "unlock";
        p.to = msg.receiver;
        p.amount = msg.amount;
        p.args["messageHash"] = msg.messageHash;
        return p;
    }
};

} // namespace cc
