#pragma once
// =====================================================================
//  Các kiểu cơ bản dùng chung toàn framework
// =====================================================================
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#ifdef CC_USE_BOOST
  #include <boost/multiprecision/cpp_int.hpp>
#endif

namespace cc {

// Số tiền / số dư.
//  - Mặc định: uint64_t (đủ cho mô phỏng, tối đa ~1.8e19 đơn vị nhỏ nhất).
//  - Bật -DCC_USE_BOOST để dùng số nguyên tùy ý độ dài (khớp uint256 của EVM).
#ifdef CC_USE_BOOST
using Amount = boost::multiprecision::cpp_int;
inline std::string amountToString(const Amount& a) { return a.str(); }
inline Amount amountFromString(const std::string& s) { return Amount(s); }
#else
using Amount = std::uint64_t;
inline std::string amountToString(const Amount& a) { return std::to_string(a); }
inline Amount amountFromString(const std::string& s) { return std::stoull(s); }
#endif

using Address = std::string;                        // địa chỉ ví / hợp đồng
using Hash    = std::string;                        // mã băm (hex)
using Bytes   = std::vector<std::uint8_t>;          // calldata thô
using KeyValue = std::map<std::string, std::string>; // storage slot, tham số

// Loại mạng blockchain (khớp cột chains.chain_type trong database)
enum class ChainType { EVM, UTXO, SOLANA, MOCK };

// Định dạng giao dịch do TransactionFactory sinh ra
enum class TxFormat { EVM, UTXO, SOLANA_INSTRUCTION };

// Kết quả thực thi giao dịch (khớp transactions.status)
enum class TxStatus { PENDING, SUCCESS, REVERTED, FAILED };

// Vòng đời một thông điệp xuyên chuỗi (khớp cross_chain_messages.status)
enum class MessageStatus { SENT, RELAYED, VERIFIED, EXECUTED, FAILED };

inline const char* toString(ChainType t) {
    switch (t) {
        case ChainType::EVM:    return "EVM";
        case ChainType::UTXO:   return "UTXO";
        case ChainType::SOLANA: return "SOLANA";
        case ChainType::MOCK:   return "MOCK";
    }
    return "?";
}

inline const char* toString(TxStatus s) {
    switch (s) {
        case TxStatus::PENDING:  return "PENDING";
        case TxStatus::SUCCESS:  return "SUCCESS";
        case TxStatus::REVERTED: return "REVERTED";
        case TxStatus::FAILED:   return "FAILED";
    }
    return "?";
}

inline const char* toString(MessageStatus s) {
    switch (s) {
        case MessageStatus::SENT:     return "SENT";
        case MessageStatus::RELAYED:  return "RELAYED";
        case MessageStatus::VERIFIED: return "VERIFIED";
        case MessageStatus::EXECUTED: return "EXECUTED";
        case MessageStatus::FAILED:   return "FAILED";
    }
    return "?";
}

} // namespace cc
