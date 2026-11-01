#pragma once
// =====================================================================
//  UtxoTransactionFactory — tạo UtxoTransaction cho chain mô hình "tờ tiền".
//   - mint     : không có tờ vào, một tờ ra cho người nhận.
//   - transfer : chọn đủ tờ chưa tiêu của người gửi, tờ ra cho người nhận,
//                phần dư trả lại người gửi (tiền thối).
// =====================================================================
#include "cc/factory/TransactionFactory.h"

#include <memory>

namespace cc {

class UtxoTransactionFactory : public TransactionFactory {
public:
    explicit UtxoTransactionFactory(std::shared_ptr<const LocalLedgerSimulator> ledger);

    using TransactionFactory::createTransaction;
    std::unique_ptr<ITransaction> createTransaction(const TxPayload& payload) override;
    ChainType chainType() const override { return ChainType::UTXO; }

    // Địa chỉ UTXO hợp lệ: 6..64 ký tự, chỉ gồm a-z, 0-9, '_'.
    static bool isValidAddress(const Address& a);

    // Chữ ký giả lập cho một tờ tiền (đủ để ExecutionEngine kiểm chủ sở hữu).
    static std::string fakeSign(const Address& owner, const std::string& utxoId);

private:
    std::shared_ptr<const LocalLedgerSimulator> ledger_;
    std::uint64_t counter_ = 0;   // giúp hai giao dịch giống nhau vẫn khác mã băm
};

} // namespace cc
