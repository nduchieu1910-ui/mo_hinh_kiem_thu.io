#pragma once
// =====================================================================
//  TransactionResult — "biên lai" sau khi thực thi một giao dịch
//  Khớp bảng transactions (phần kết quả) và events.
// =====================================================================
#include "cc/core/Types.h"

#include <string>
#include <vector>

namespace cc {

// Sự kiện (log) phát ra khi thực thi, ví dụ "Locked", "Minted".
// Bridge lắng nghe các sự kiện này để biết khi nào cần chuyển thư.
struct Event {
    std::string name;        // tên sự kiện
    Address     contract;    // hợp đồng phát ra sự kiện
    KeyValue    fields;      // dữ liệu kèm theo
};

struct TransactionResult {
    Hash               txHash;
    std::string        chain;          // chain đã thực thi
    TxStatus           status = TxStatus::PENDING;
    std::uint64_t      gasUsed = 0;
    std::string        revertReason;   // lý do khi REVERTED / FAILED
    std::vector<Event> events;

    bool ok() const { return status == TxStatus::SUCCESS; }

    static TransactionResult success(Hash hash, std::string chainName,
                                     std::uint64_t gas = 0) {
        TransactionResult r;
        r.txHash = std::move(hash);
        r.chain = std::move(chainName);
        r.status = TxStatus::SUCCESS;
        r.gasUsed = gas;
        return r;
    }

    static TransactionResult failure(TxStatus st, std::string reason,
                                     std::string chainName = "") {
        TransactionResult r;
        r.status = st;
        r.revertReason = std::move(reason);
        r.chain = std::move(chainName);
        return r;
    }
};

} // namespace cc
