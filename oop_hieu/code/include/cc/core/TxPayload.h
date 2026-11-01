#pragma once
// =====================================================================
//  TxPayload — "yêu cầu chung" mà Bridge gửi xuống Adapter.
//  Không phụ thuộc định dạng chain nào; Adapter sẽ nhờ TransactionFactory
//  đổi nó thành giao dịch đúng kiểu (EVM / UTXO...).
// =====================================================================
#include "cc/core/Types.h"

#include <string>

namespace cc {

struct TxPayload {
    std::string action;   // "transfer", "lock", "mint", "call"...
    Address     from;
    Address     to;
    Amount      amount = 0;
    KeyValue    args;     // tham số thêm (token, method, data...)
};

} // namespace cc
