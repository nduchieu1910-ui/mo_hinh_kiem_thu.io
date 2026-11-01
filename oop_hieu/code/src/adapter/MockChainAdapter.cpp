#include "cc/adapter/MockChainAdapter.h"

#include "cc/core/HashUtil.h"

namespace cc {

TransactionResult MockChainAdapter::sendTransaction(const TxPayload& p) {
    ++txCount_;

    // Lỗi được cài sẵn cho test: trả về ngay, không đổi số dư.
    if (!faults_.empty()) {
        Fault f = faults_.front();
        faults_.pop_front();
        return TransactionResult::failure(f.status, f.reason, name_);
    }
    if (p.amount == 0) return TransactionResult::failure(TxStatus::FAILED, "số tiền phải lớn hơn 0", name_);

    const Hash h = simHash(name_ + "|" + std::to_string(txCount_));
    std::vector<Event> events;

    if (p.action == "mint") {
        balances_[p.to] += p.amount;
        auto it = p.args.find("messageHash");
        events.push_back({"Minted", "", {{"receiver", p.to}, {"amount", amountToString(p.amount)},
                                         {"messageHash", it == p.args.end() ? "" : it->second}}});
    } else if (p.action == "transfer" || p.action == "lock") {
        if (getBalance(p.from) < p.amount)
            return TransactionResult::failure(TxStatus::REVERTED, "không đủ tiền", name_);
        balances_[p.from] -= p.amount;
        if (p.action == "transfer") {
            balances_[p.to] += p.amount;
            events.push_back({"Transferred", "", {{"from", p.from}, {"to", p.to}, {"amount", amountToString(p.amount)}}});
        } else {
            balances_["escrow"] += p.amount;
            auto arg = [&](const std::string& k) { auto it = p.args.find(k); return it == p.args.end() ? std::string() : it->second; };
            events.push_back({"Locked", "escrow", {{"sender", p.from}, {"receiver", arg("receiver")},
                                                   {"amount", amountToString(p.amount)}, {"txHash", h},
                                                   {"targetChain", arg("targetChain")}}});
        }
    } else {
        return TransactionResult::failure(TxStatus::FAILED, "Mock không hỗ trợ hành động: " + p.action, name_);
    }

    auto r = TransactionResult::success(h, name_, 0);
    r.events = events;
    emit(events);
    return r;
}

} // namespace cc
