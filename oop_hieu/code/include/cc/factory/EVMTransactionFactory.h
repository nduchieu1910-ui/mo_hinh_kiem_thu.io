#pragma once
// =====================================================================
//  EVMTransactionFactory — tạo EvmTransaction cho chain mô hình tài khoản.
//  Tự điền: nonce (đọc từ sổ), gasPrice, gasLimit (theo loại hành động),
//  data (mã hóa hành động + tham số), txHash.
// =====================================================================
#include "cc/factory/TransactionFactory.h"

#include <memory>

namespace cc {

class EVMTransactionFactory : public TransactionFactory {
public:
    explicit EVMTransactionFactory(std::shared_ptr<const LocalLedgerSimulator> ledger,
                                   Amount defaultGasPrice = 0);

    using TransactionFactory::createTransaction;
    std::unique_ptr<ITransaction> createTransaction(const TxPayload& payload) override;
    ChainType chainType() const override { return ChainType::EVM; }

    // Địa chỉ EVM hợp lệ: "0x" + 1..40 ký tự hex.
    static bool isValidAddress(const Address& a);

    // Gas mặc định cho từng hành động (args["gasLimit"] ghi đè được).
    static std::uint64_t defaultGasLimit(const std::string& action);

private:
    std::shared_ptr<const LocalLedgerSimulator> ledger_;
    Amount defaultGasPrice_;
};

} // namespace cc
