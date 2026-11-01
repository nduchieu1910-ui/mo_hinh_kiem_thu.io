#include "cc/factory/UtxoTransactionFactory.h"

#include "cc/core/HashUtil.h"
#include "cc/ledger/LocalLedgerSimulator.h"

namespace cc {

UtxoTransactionFactory::UtxoTransactionFactory(std::shared_ptr<const LocalLedgerSimulator> ledger)
    : ledger_(std::move(ledger)) {
    if (!ledger_) throw std::invalid_argument("UtxoTransactionFactory: thiếu sổ cái");
}

bool UtxoTransactionFactory::isValidAddress(const Address& a) {
    if (a.size() < 6 || a.size() > 64) return false;
    for (char c : a)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

std::string UtxoTransactionFactory::fakeSign(const Address& owner, const std::string& utxoId) {
    return simSign(owner, utxoId);
}

std::unique_ptr<ITransaction> UtxoTransactionFactory::createTransaction(const TxPayload& p) {
    if (!isValidAddress(p.to)) throw TxBuildError("UTXO: địa chỉ người nhận sai mẫu: " + p.to);
    if (p.amount == 0) throw TxBuildError("UTXO: số tiền phải lớn hơn 0");

    auto tx = std::make_unique<UtxoTransaction>();
    tx->meta = p.args;
    tx->meta["action"] = p.action;

    if (p.action == "mint") {
        // Phát tiền mới (bridge mint): không có tờ vào.
        tx->outputs.push_back({p.to, p.amount});
    } else if (p.action == "transfer") {
        if (!isValidAddress(p.from)) throw TxBuildError("UTXO: địa chỉ người gửi sai mẫu: " + p.from);

        // Chọn tờ tiền chưa tiêu cho tới khi đủ số tiền cần trả.
        Amount collected = 0;
        for (const Utxo& u : ledger_->unspentOf(p.from)) {
            tx->inputs.push_back({u.id, fakeSign(p.from, u.id)});
            collected += u.amount;
            if (collected >= p.amount) break;
        }
        if (collected < p.amount)
            throw TxBuildError("UTXO: không đủ tờ tiền, có " + amountToString(collected) +
                               ", cần " + amountToString(p.amount));

        tx->outputs.push_back({p.to, p.amount});
        if (collected > p.amount) tx->outputs.push_back({p.from, collected - p.amount});  // tiền thối
    } else {
        throw TxBuildError("UTXO: không hỗ trợ hành động '" + p.action + "'");
    }

    std::string raw = p.action + "|" + std::to_string(++counter_);
    for (const auto& in : tx->inputs) raw += "|in:" + in.utxoId;
    for (const auto& out : tx->outputs) raw += "|out:" + out.owner + "=" + amountToString(out.amount);
    tx->txHash = simHash(raw);
    return tx;
}

} // namespace cc
