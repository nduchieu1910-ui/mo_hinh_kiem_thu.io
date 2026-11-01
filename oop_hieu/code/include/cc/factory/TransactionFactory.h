#pragma once
// =====================================================================
//  TransactionFactory — FACTORY PATTERN (Abstract Factory)
//  Chuẩn hóa việc tạo cấu trúc giao dịch phức tạp của từng chain.
//
//  Cách gọi đúng đề tài:
//      auto tx = TransactionFactory::createTransaction(ChainType::EVM, payload, ledger);
//  Hoặc giữ một factory cụ thể và gọi nhiều lần:
//      auto f = TransactionFactory::forChain(ChainType::EVM, ledger);
//      auto tx = f->createTransaction(payload);
// =====================================================================
#include "cc/core/TxPayload.h"
#include "cc/core/Types.h"
#include "cc/factory/ITransaction.h"

#include <memory>
#include <stdexcept>
#include <string>

namespace cc {

class LocalLedgerSimulator;

// Lỗi khi không dựng được giao dịch (địa chỉ sai mẫu, thiếu tờ tiền...).
// Adapter bắt lỗi này và trả TransactionResult FAILED.
class TxBuildError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class TransactionFactory {
public:
    virtual ~TransactionFactory() = default;

    // Tạo giao dịch đúng định dạng của chain mà factory phụ trách.
    // Ném TxBuildError nếu yêu cầu không hợp lệ.
    virtual std::unique_ptr<ITransaction> createTransaction(const TxPayload& payload) = 0;

    // Loại chain mà factory phục vụ.
    virtual ChainType chainType() const = 0;

    // createTransaction(ChainType, Payload) — đúng chữ ký trong đề tài.
    // Cần thêm sổ cái để factory đọc nonce / tờ tiền / số khối.
    static std::unique_ptr<ITransaction>
    createTransaction(ChainType type, const TxPayload& payload,
                      std::shared_ptr<const LocalLedgerSimulator> ledger) {
        return forChain(type, std::move(ledger))->createTransaction(payload);
    }

    // Chọn factory cụ thể theo loại chain.
    static std::unique_ptr<TransactionFactory>
    forChain(ChainType type, std::shared_ptr<const LocalLedgerSimulator> ledger);
};

} // namespace cc
