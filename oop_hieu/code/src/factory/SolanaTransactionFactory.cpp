#include "cc/factory/SolanaTransactionFactory.h"

#include "cc/core/HashUtil.h"
#include "cc/ledger/LocalLedgerSimulator.h"

#include <cstring>

namespace cc {

SolanaTransactionFactory::SolanaTransactionFactory(std::shared_ptr<const LocalLedgerSimulator> ledger)
    : ledger_(std::move(ledger)) {
    if (!ledger_) throw std::invalid_argument("SolanaTransactionFactory: thiếu sổ cái");
}

bool SolanaTransactionFactory::isValidAddress(const Address& a) {
    static const char* kBase58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    if (a.size() < 32 || a.size() > 44) return false;
    for (char c : a)
        if (!std::strchr(kBase58, c)) return false;
    return true;
}

const Address& SolanaTransactionFactory::systemAccount() {
    static const Address kSystem(32, '1');   // "1111...1" như System Program của Solana
    return kSystem;
}

std::unique_ptr<ITransaction> SolanaTransactionFactory::createTransaction(const TxPayload& p) {
    const bool lending = p.action == "borrow" || p.action == "repay" || p.action == "liquidate";
    if (!lending && p.action != "transfer" && p.action != "lock" && p.action != "unlock" && p.action != "mint")
        throw TxBuildError("Solana: không hỗ trợ hành động '" + p.action + "'");
    if (p.amount == 0) throw TxBuildError("Solana: số lamports phải lớn hơn 0");

    const Address payer = p.from.empty() ? systemAccount() : p.from;
    const Address to = p.to.empty() ? payer : p.to;   // lock không cần người nhận trên chain này
    if (!isValidAddress(payer)) throw TxBuildError("Solana: địa chỉ người gửi sai mẫu: " + payer);
    if (!isValidAddress(to)) throw TxBuildError("Solana: địa chỉ người nhận sai mẫu: " + to);

    auto ix = std::make_unique<SolanaInstruction>();
    ix->programId = p.action == "transfer" ? "system" : (lending ? "lending" : "bridge");
    ix->action = p.action;
    ix->feePayer = payer;
    ix->to = to;
    ix->amount = p.amount;
    ix->accounts = {payer, to};
    ix->args = p.args;
    ix->recentBlockhash = "bh:" + std::to_string(ledger_->blockHeight());
    ix->txHash = simHash(ix->programId + "|" + p.action + "|" + payer + "|" + to + "|" +
                         amountToString(p.amount) + "|" + ix->recentBlockhash + "|" + std::to_string(++counter_));
    ix->signatures = {simSign(payer, ix->txHash)};
    return ix;
}

} // namespace cc
