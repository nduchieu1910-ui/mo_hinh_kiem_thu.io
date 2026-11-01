#pragma once
// =====================================================================
//  SolanaTransactionFactory — tạo SolanaInstruction:
//    programId ("system" cho transfer, "bridge" cho lock/unlock/mint),
//    accounts, recentBlockhash (đọc số khối hiện tại), signatures.
// =====================================================================
#include "cc/factory/TransactionFactory.h"

#include <memory>

namespace cc {

class SolanaTransactionFactory : public TransactionFactory {
public:
    explicit SolanaTransactionFactory(std::shared_ptr<const LocalLedgerSimulator> ledger);

    using TransactionFactory::createTransaction;
    std::unique_ptr<ITransaction> createTransaction(const TxPayload& payload) override;
    ChainType chainType() const override { return ChainType::SOLANA; }

    // Địa chỉ Solana hợp lệ: 32..44 ký tự base58 (không có 0, O, I, l).
    static bool isValidAddress(const Address& a);

    // Tài khoản hệ thống, dùng làm người gửi khi bridge mint.
    static const Address& systemAccount();

private:
    std::shared_ptr<const LocalLedgerSimulator> ledger_;
    std::uint64_t counter_ = 0;
};

} // namespace cc
