#pragma once
// =====================================================================
//  MockChainAdapter — chain giả cực nhẹ, chạy hoàn toàn trong bộ nhớ.
//  Không dùng factory hay sổ cái: chỉ một bảng số dư.
//  Dùng để test nhanh và để GÂY LỖI CÓ CHỦ ĐÍCH (node sập, hết gas...)
//  mà không phải dựng chain thật.
// =====================================================================
#include "cc/adapter/AdapterBase.h"

#include <deque>
#include <map>
#include <string>

namespace cc {

class MockChainAdapter : public AdapterBase {
public:
    explicit MockChainAdapter(std::string name) : name_(std::move(name)) {}

    ChainType   chainType() const override { return ChainType::MOCK; }
    std::string chainName() const override { return name_; }

    Amount getBalance(const Address& a) const override {
        auto it = balances_.find(a);
        return it == balances_.end() ? Amount(0) : it->second;
    }

    TransactionResult  sendTransaction(const TxPayload& payload) override;
    SmartContractState getContractState(const Address& contract) const override {
        return SmartContractState{contract, name_, "", {}};
    }

    // ----- Công cụ cho test -----------------------------------------
    void setBalance(const Address& a, Amount v) { balances_[a] = v; }

    // `count` giao dịch tiếp theo sẽ thất bại với trạng thái/lý do cho trước.
    void injectFailure(TxStatus status, std::string reason, int count = 1) {
        for (int i = 0; i < count; ++i) faults_.push_back({status, reason});
    }

    std::size_t txCount() const { return txCount_; }
    const std::map<Address, Amount>& balances() const { return balances_; }

    struct Fault { TxStatus status; std::string reason; };

    // Memento: toàn bộ trạng thái của chain giả (Sandbox chụp / khôi phục).
    struct State {
        std::map<Address, Amount> balances;
        std::deque<Fault>         faults;
        std::size_t               txCount = 0;
    };
    State saveState() const { return {balances_, faults_, txCount_}; }
    void restoreState(const State& s) { balances_ = s.balances; faults_ = s.faults; txCount_ = s.txCount; }

private:

    std::string                name_;
    std::map<Address, Amount>  balances_;
    std::deque<Fault>          faults_;
    std::size_t                txCount_ = 0;
};

} // namespace cc
