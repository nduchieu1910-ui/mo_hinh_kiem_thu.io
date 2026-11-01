#pragma once
// =====================================================================
//  SolanaAdapter — ADAPTER cho chain kiểu Solana.
//  Bọc node mô phỏng (LocalLedgerSimulator) thay cho Solana Web3 SDK.
//  Giao dịch là SolanaInstruction gọi một program:
//    program "system" : transfer
//    program "bridge" : lock (phát "Locked") · unlock · mint (phát "Minted")
//  Số dư tính bằng lamports.
// =====================================================================
#include "cc/adapter/LedgerBackedAdapter.h"

namespace cc {

class SolanaAdapter : public LedgerBackedAdapter {
public:
    explicit SolanaAdapter(std::shared_ptr<LocalLedgerSimulator> ledger,
                           Address bridgeVault = "BridgeVau1t11111111111111111111111",
                           std::unique_ptr<TransactionFactory> factory = nullptr)
        : LedgerBackedAdapter(std::move(ledger), std::move(factory), ChainType::SOLANA, std::move(bridgeVault)) {}

    ChainType chainType() const override { return ChainType::SOLANA; }
};

} // namespace cc
