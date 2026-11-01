#pragma once
// =====================================================================
//  EVMAdapter — ADAPTER cho chain kiểu EVM (mô hình tài khoản).
//  Bọc node mô phỏng (LocalLedgerSimulator). Muốn nối Hardhat/Ganache thật:
//  thay phần bên trong bằng lời gọi JSON-RPC, interface giữ nguyên.
//
//  Hành động: transfer · lock (phát "Locked") · unlock · mint (phát "Minted")
// =====================================================================
#include "cc/adapter/LedgerBackedAdapter.h"

namespace cc {

class EVMAdapter : public LedgerBackedAdapter {
public:
    explicit EVMAdapter(std::shared_ptr<LocalLedgerSimulator> ledger,
                        Address escrow = "0xb41d6e",
                        std::unique_ptr<TransactionFactory> factory = nullptr)
        : LedgerBackedAdapter(std::move(ledger), std::move(factory), ChainType::EVM, std::move(escrow)) {}

    ChainType chainType() const override { return ChainType::EVM; }

    const Address& escrow() const { return bridgeAccount(); }
};

} // namespace cc
