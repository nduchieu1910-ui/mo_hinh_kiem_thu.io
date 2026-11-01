#pragma once
// =====================================================================
//  ITransaction — giao dịch cụ thể của một loại chain.
//  Các lớp con giữ cấu trúc riêng: EvmTransaction, UtxoTransaction,
//  SolanaInstruction.
// =====================================================================
#include "cc/core/Types.h"

#include <string>
#include <vector>

namespace cc {

class ITransaction {
public:
    virtual ~ITransaction() = default;
    virtual Hash     hash() const = 0;
    virtual TxFormat format() const = 0;
};

// ----- Giao dịch kiểu EVM (mô hình tài khoản) -------------------------
struct EvmTransaction final : ITransaction {
    Address       from;
    Address       to;
    Amount        value = 0;
    std::uint64_t nonce = 0;
    Amount        gasPrice = 0;
    std::uint64_t gasLimit = 0;
    Bytes         data;          // calldata: "action|k=v|..."
    std::string   signature;     // chữ ký của `from` trên txHash
    Hash          txHash;

    Hash     hash() const override { return txHash; }
    TxFormat format() const override { return TxFormat::EVM; }
};

// ----- Giao dịch kiểu UTXO (mô hình "tờ tiền") ------------------------
struct TxInput {
    std::string utxoId;          // tờ tiền đem ra tiêu
    std::string signature;       // chữ ký của chủ tờ
};

struct TxOutput {
    Address owner;               // người nhận tờ tiền mới
    Amount  amount = 0;
};

struct UtxoTransaction final : ITransaction {
    std::vector<TxInput>  inputs;
    std::vector<TxOutput> outputs;
    KeyValue              meta;          // thông tin kèm (vd: action, messageHash)
    Hash                  txHash;

    // Danh sách chữ ký (mỗi tờ vào một chữ ký) — đúng cấu trúc đề tài.
    std::vector<std::string> signatures() const {
        std::vector<std::string> s;
        for (const auto& in : inputs) s.push_back(in.signature);
        return s;
    }

    Hash     hash() const override { return txHash; }
    TxFormat format() const override { return TxFormat::UTXO; }
};

// ----- Giao dịch kiểu Solana (instruction gọi một program) ------------
struct SolanaInstruction final : ITransaction {
    std::string              programId;        // "system" (chuyển tiền) | "bridge"
    std::string              action;           // transfer | lock | unlock | mint
    Address                  feePayer;         // người ký & trả phí
    Address                  to;
    Amount                   amount = 0;       // lamports
    std::vector<Address>     accounts;         // các tài khoản instruction đụng tới
    KeyValue                 args;
    std::string              recentBlockhash;  // "bh:<height>", hết hạn sau 150 khối
    std::vector<std::string> signatures;       // chữ ký của feePayer trên txHash
    Hash                     txHash;

    Hash     hash() const override { return txHash; }
    TxFormat format() const override { return TxFormat::SOLANA_INSTRUCTION; }
};

} // namespace cc
