#pragma once
// =====================================================================
//  UtxoAdapter — ADAPTER cho chain kiểu UTXO (mô hình "tờ tiền", như Bitcoin).
//  Hành động: mint (phát tờ mới) · transfer (tiêu tờ cũ, tạo tờ mới).
// =====================================================================
#include "cc/adapter/LedgerBackedAdapter.h"

namespace cc {

class UtxoAdapter : public LedgerBackedAdapter {
public:
    explicit UtxoAdapter(std::shared_ptr<LocalLedgerSimulator> ledger,
                         std::unique_ptr<TransactionFactory> factory = nullptr)
        : LedgerBackedAdapter(std::move(ledger), std::move(factory), ChainType::UTXO, "utxo_bridge") {}

    ChainType chainType() const override { return ChainType::UTXO; }

    // Số dư UTXO = tổng các tờ chưa tiêu.
    Amount getBalance(const Address& address) const override { return ledger_->utxoBalanceOf(address); }
};

} // namespace cc
