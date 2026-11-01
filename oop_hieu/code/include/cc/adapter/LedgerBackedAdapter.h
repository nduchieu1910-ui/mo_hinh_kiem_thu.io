#pragma once
// =====================================================================
//  LedgerBackedAdapter — khung chung cho adapter chạy trên node mô phỏng
//  (LocalLedgerSimulator). Quy trình sendTransaction() giống nhau ở mọi chain:
//
//     TxPayload --(TransactionFactory)--> ITransaction --(ExecutionEngine)--> TransactionResult
//
//  Lớp con chỉ cần nói mình là chain gì và cách đọc số dư.
// =====================================================================
#include "cc/adapter/AdapterBase.h"
#include "cc/engine/ExecutionEngine.h"
#include "cc/factory/TransactionFactory.h"
#include "cc/ledger/LocalLedgerSimulator.h"

#include <memory>

namespace cc {

class LedgerBackedAdapter : public AdapterBase {
public:
    std::string chainName() const override { return ledger_->chainName(); }

    Amount getBalance(const Address& address) const override { return ledger_->balanceOf(address); }

    TransactionResult sendTransaction(const TxPayload& payload) override {
        try {
            auto tx = factory_->createTransaction(payload);   // Factory dựng phiếu đúng mẫu
            return submit(*tx);                                // ExecutionEngine thực thi
        } catch (const TxBuildError& e) {
            return TransactionResult::failure(TxStatus::FAILED, e.what(), chainName());
        }
    }

    SmartContractState getContractState(const Address& contract) const override {
        return ledger_->getContract(contract).value_or(SmartContractState{contract, chainName(), "", {}});
    }

    // Gửi thẳng một giao dịch đã dựng sẵn (dùng trong test: phát lại, tiêu 2 lần...).
    TransactionResult submit(const ITransaction& tx) {
        TransactionResult r = engine_.execute(tx);
        if (r.ok()) emit(r.events);   // báo cho người đang nghe (Relayer)
        return r;
    }

    ExecutionEngine&       engine() { return engine_; }
    const Address&         bridgeAccount() const { return engine_.bridgeAccount(); }

protected:
    LedgerBackedAdapter(std::shared_ptr<LocalLedgerSimulator> ledger,
                        std::unique_ptr<TransactionFactory> factory,
                        ChainType expected, Address bridgeAccount)
        : ledger_(checked(std::move(ledger))),
          factory_(factory ? std::move(factory) : TransactionFactory::forChain(expected, ledger_)),
          engine_(ledger_, std::move(bridgeAccount)) {
        if (factory_->chainType() != expected)
            throw std::invalid_argument(std::string("Adapter: factory không khớp loại chain ") + toString(expected));
    }

    std::shared_ptr<LocalLedgerSimulator> ledger_;
    std::unique_ptr<TransactionFactory>   factory_;
    ExecutionEngine                       engine_;

private:
    static std::shared_ptr<LocalLedgerSimulator> checked(std::shared_ptr<LocalLedgerSimulator> l) {
        if (!l) throw std::invalid_argument("Adapter: thiếu sổ cái");
        return l;
    }
};

} // namespace cc
