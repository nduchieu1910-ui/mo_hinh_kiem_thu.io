#include "cc/adapter/UtxoAdapter.h"

#include "cc/factory/UtxoTransactionFactory.h"

#include <set>

namespace cc {

UtxoAdapter::UtxoAdapter(std::shared_ptr<LocalLedgerSimulator> ledger,
                         std::unique_ptr<TransactionFactory> factory)
    : ledger_(std::move(ledger)), factory_(std::move(factory)) {
    if (!ledger_) throw std::invalid_argument("UtxoAdapter: thiếu sổ cái");
    if (!factory_) factory_ = std::make_unique<UtxoTransactionFactory>(ledger_);
    if (factory_->chainType() != ChainType::UTXO)
        throw std::invalid_argument("UtxoAdapter: factory không phải loại UTXO");
}

Amount UtxoAdapter::getBalance(const Address& address) const {
    return ledger_->utxoBalanceOf(address);
}

SmartContractState UtxoAdapter::getContractState(const Address& contract) const {
    return ledger_->getContract(contract).value_or(SmartContractState{contract, chainName(), "", {}});
}

TransactionResult UtxoAdapter::sendTransaction(const TxPayload& payload) {
    try {
        auto tx = factory_->createTransaction(payload);   // Factory dựng phiếu UTXO
        auto* utxo = dynamic_cast<UtxoTransaction*>(tx.get());
        if (!utxo) return TransactionResult::failure(TxStatus::FAILED, "factory trả sai loại giao dịch", chainName());
        return execute(*utxo);
    } catch (const TxBuildError& e) {
        return TransactionResult::failure(TxStatus::FAILED, e.what(), chainName());
    }
}

TransactionResult UtxoAdapter::execute(const UtxoTransaction& tx) {
    auto fail = [&](const std::string& reason) {
        auto r = TransactionResult::failure(TxStatus::REVERTED, reason, chainName());
        r.txHash = tx.txHash;
        return r;
    };

    if (tx.outputs.empty()) return fail("giao dịch không có tờ ra");

    // 1. Kiểm từng tờ vào (chưa ghi gì vào sổ).
    std::set<std::string> seen;
    Amount totalIn = 0, totalOut = 0;
    Address firstOwner;
    for (const TxInput& in : tx.inputs) {
        if (!seen.insert(in.utxoId).second) return fail("dùng một tờ hai lần trong cùng giao dịch: " + in.utxoId);
        auto u = ledger_->getUtxo(in.utxoId);
        if (!u) return fail("không tìm thấy tờ: " + in.utxoId);
        if (u->spent) return fail("UTXO đã được tiêu: " + in.utxoId);
        if (in.signature != UtxoTransactionFactory::fakeSign(u->owner, in.utxoId))
            return fail("chữ ký không đúng chủ tờ: " + in.utxoId);
        if (firstOwner.empty()) firstOwner = u->owner;
        totalIn += u->amount;
    }
    for (const TxOutput& out : tx.outputs) {
        if (out.amount == 0) return fail("tờ ra có giá trị 0");
        totalOut += out.amount;
    }

    auto meta = [&](const std::string& k) { auto it = tx.meta.find(k); return it == tx.meta.end() ? std::string() : it->second; };
    const bool isMint = tx.inputs.empty();
    if (isMint && meta("action") != "mint") return fail("giao dịch không có tờ vào");
    if (!isMint && totalIn < totalOut) return fail("tổng tờ ra lớn hơn tổng tờ vào");

    // 2. Ghi vào sổ: tiêu tờ cũ, tạo tờ mới.
    for (const TxInput& in : tx.inputs) ledger_->spendUtxo(in.utxoId);
    for (std::size_t i = 0; i < tx.outputs.size(); ++i)
        ledger_->addUtxo({tx.txHash + ":" + std::to_string(i), tx.outputs[i].owner, tx.outputs[i].amount, false});
    ledger_->mineBlock();

    std::vector<Event> events;
    if (isMint)
        events.push_back({"Minted", "", {{"receiver", tx.outputs[0].owner}, {"amount", amountToString(tx.outputs[0].amount)},
                                         {"messageHash", meta("messageHash")}}});
    else
        events.push_back({"Transferred", "", {{"from", firstOwner}, {"to", tx.outputs[0].owner},
                                              {"amount", amountToString(tx.outputs[0].amount)}}});

    auto r = TransactionResult::success(tx.txHash, chainName(), 0);
    r.events = events;
    emit(events);
    return r;
}

} // namespace cc
