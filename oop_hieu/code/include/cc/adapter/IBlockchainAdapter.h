#pragma once
// =====================================================================
//  IBlockchainAdapter — ADAPTER PATTERN
//  "Ngôn ngữ chung" mà framework dùng để nói chuyện với mọi blockchain.
//  Mỗi chain có một lớp con: EVMAdapter, UtxoAdapter, MockChainAdapter...
//  Đồng thời là phía IMPLEMENTOR của BRIDGE PATTERN (xem CrossChainBridge).
// =====================================================================
#include "cc/core/SmartContractState.h"
#include "cc/core/TransactionResult.h"
#include "cc/core/TxPayload.h"
#include "cc/core/Types.h"

#include <functional>
#include <string>

namespace cc {

using EventCallback = std::function<void(const Event&)>;

class IBlockchainAdapter {
public:
    virtual ~IBlockchainAdapter() = default;

    // Thông tin mạng
    virtual ChainType   chainType() const = 0;
    virtual std::string chainName() const = 0;

    // Đọc số dư của một địa chỉ
    virtual Amount getBalance(const Address& address) const = 0;

    // Gửi một yêu cầu chung; adapter tự nhờ TransactionFactory
    // tạo giao dịch đúng kiểu rồi đẩy xuống sổ cái để thực thi.
    virtual TransactionResult sendTransaction(const TxPayload& payload) = 0;

    // Đọc trạng thái (storage) của một hợp đồng
    virtual SmartContractState getContractState(const Address& contract) const = 0;

    // Đăng ký lắng nghe một loại sự kiện, ví dụ "Locked"
    virtual void listenEvent(const std::string& eventName, EventCallback callback) = 0;
};

} // namespace cc
