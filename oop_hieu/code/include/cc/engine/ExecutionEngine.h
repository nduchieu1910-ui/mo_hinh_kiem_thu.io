#pragma once
// =====================================================================
//  ExecutionEngine — nhận một giao dịch (ITransaction) và:
//    1. kiểm chữ ký
//    2. tính gas giả lập (EVM: gas, UTXO: theo kích thước, Solana: compute unit)
//    3. cập nhật trạng thái trên LocalLedgerSimulator
//    4. ROLLBACK: chụp sổ trước khi ghi, gặp Revert giữa chừng thì trả sổ về như cũ
//
//  Adapter không tự thực thi nữa — chỉ dựng phiếu (qua Factory) rồi giao
//  cho ExecutionEngine.
// =====================================================================
#include "cc/core/TransactionResult.h"
#include "cc/factory/ITransaction.h"
#include "cc/ledger/LocalLedgerSimulator.h"

#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace cc {

// Lệnh Revert trong lúc thực thi -> ExecutionEngine rollback toàn bộ giao dịch.
class RevertError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ExecutionEngine {
public:
    // bridgeAccount = tài khoản/két của hợp đồng bridge trên chain này.
    ExecutionEngine(std::shared_ptr<LocalLedgerSimulator> ledger, Address bridgeAccount);

    TransactionResult execute(const ITransaction& tx);

    const Address& bridgeAccount() const { return bridge_; }

    // Memento: danh sách giao dịch Solana đã xử lý (Sandbox chụp / khôi phục).
    std::set<Hash> saveState() const { return processedSolana_; }
    void restoreState(const std::set<Hash>& s) { processedSolana_ = s; }

    // Phí Solana cho mỗi chữ ký (mặc định 0 để số dư trong test dễ đọc).
    void setLamportsPerSignature(Amount v) { lamportsPerSig_ = v; }

    // ----- Bảng gas giả lập ------------------------------------------
    static std::uint64_t evmIntrinsicGas(const std::string& action);
    static std::uint64_t utxoGas(const UtxoTransaction& tx);          // 100/tờ vào + 50/tờ ra
    static std::uint64_t solanaComputeUnits(const SolanaInstruction& ix);

    static constexpr std::uint64_t kSolanaBlockhashTtl = 150;           // số khối blockhash còn hiệu lực

private:
    TransactionResult executeEvm(const EvmTransaction& tx);
    TransactionResult executeUtxo(const UtxoTransaction& tx);
    TransactionResult executeSolana(const SolanaInstruction& ix);

    // Áp một hành động lên mô hình tài khoản (dùng chung cho EVM và Solana).
    // Ném RevertError nếu không hợp lệ (vd: thiếu tiền).
    std::vector<Event> applyAccountAction(const std::string& action, const Address& from,
                                          const Address& to, Amount value,
                                          const KeyValue& args, const Hash& txHash);

    void rollback(const LedgerSnapshot& snapshot) { ledger_->restore(snapshot); }

    TransactionResult reverted(const Hash& h, const std::string& reason) const;

    std::shared_ptr<LocalLedgerSimulator> ledger_;
    Address                               bridge_;
    Amount                                lamportsPerSig_ = 0;
    std::set<Hash>                        processedSolana_;   // chống gửi trùng giao dịch Solana
};

} // namespace cc
