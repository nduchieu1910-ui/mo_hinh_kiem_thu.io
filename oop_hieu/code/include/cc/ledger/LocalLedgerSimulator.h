#pragma once
// =====================================================================
//  LocalLedgerSimulator — sổ cái giả lập chạy trong bộ nhớ.
//  Hỗ trợ cả 2 mô hình: tài khoản (EVM) và tờ tiền (UTXO).
//
//  LedgerSnapshot — MEMENTO PATTERN: ảnh chụp toàn bộ sổ để khôi phục
//  khi một kịch bản test thất bại. Mỗi chain có một ledger riêng
//  (không dùng Singleton) để các test độc lập với nhau.
// =====================================================================
#include "cc/core/SmartContractState.h"
#include "cc/core/Types.h"

#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace cc {

struct Account {
    Address       address;
    Amount        balance = 0;
    std::uint64_t nonce = 0;
    bool          isContract = false;
};

struct Utxo {
    std::string id;          // "<txHash>:<outputIndex>"
    Address     owner;
    Amount      amount = 0;
    bool        spent = false;
};

class LocalLedgerSimulator;

// Memento: chỉ LocalLedgerSimulator được đọc nội dung bên trong.
class LedgerSnapshot {
public:
    std::uint64_t blockHeight() const { return blockHeight_; }
    const std::string& label() const { return label_; }

private:
    friend class LocalLedgerSimulator;
    std::string                               label_;
    std::uint64_t                             blockHeight_ = 0;
    std::map<Address, Account>                accounts_;
    std::map<Address, SmartContractState>     contracts_;
    std::map<std::string, Utxo>               utxos_;
};

class LocalLedgerSimulator {
public:
    explicit LocalLedgerSimulator(std::string chainName)
        : chainName_(std::move(chainName)) {}

    const std::string& chainName() const { return chainName_; }
    std::uint64_t blockHeight() const { return blockHeight_; }
    void mineBlock() { ++blockHeight_; }

    // ----- Mô hình tài khoản (EVM) -----------------------------------
    std::optional<Account> getAccount(const Address& addr) const {
        auto it = accounts_.find(addr);
        if (it == accounts_.end()) return std::nullopt;
        return it->second;
    }

    void upsertAccount(const Account& acc) { accounts_[acc.address] = acc; }

    Amount balanceOf(const Address& addr) const {
        auto acc = getAccount(addr);
        return acc ? acc->balance : Amount(0);
    }

    // ----- Trạng thái hợp đồng ---------------------------------------
    void upsertContract(const SmartContractState& st) { contracts_[st.contractAddress] = st; }

    std::optional<SmartContractState> getContract(const Address& addr) const {
        auto it = contracts_.find(addr);
        if (it == contracts_.end()) return std::nullopt;
        return it->second;
    }

    // ----- Mô hình tờ tiền (UTXO) ------------------------------------
    void addUtxo(const Utxo& u) {
        if (utxos_.count(u.id)) throw std::invalid_argument("UTXO đã tồn tại: " + u.id);
        utxos_[u.id] = u;
    }

    // Đánh dấu một tờ là đã tiêu; ném lỗi nếu không có hoặc đã tiêu (chống double-spend).
    void spendUtxo(const std::string& id) {
        auto it = utxos_.find(id);
        if (it == utxos_.end()) throw std::invalid_argument("Không tìm thấy UTXO: " + id);
        if (it->second.spent)    throw std::logic_error("UTXO đã được tiêu: " + id);
        it->second.spent = true;
    }

    std::optional<Utxo> getUtxo(const std::string& id) const {
        auto it = utxos_.find(id);
        if (it == utxos_.end()) return std::nullopt;
        return it->second;
    }

    // Các tờ chưa tiêu của một người (theo thứ tự id, ổn định cho test).
    std::vector<Utxo> unspentOf(const Address& owner) const {
        std::vector<Utxo> out;
        for (const auto& [id, u] : utxos_)
            if (u.owner == owner && !u.spent) out.push_back(u);
        return out;
    }

    Amount utxoBalanceOf(const Address& owner) const {
        Amount total = 0;
        for (const auto& [id, u] : utxos_)
            if (u.owner == owner && !u.spent) total += u.amount;
        return total;
    }

    // ----- Đọc toàn bộ (cho Sandbox so sánh thay đổi trạng thái) ------
    const std::map<Address, Account>&            accounts() const { return accounts_; }
    const std::map<Address, SmartContractState>& contracts() const { return contracts_; }
    const std::map<std::string, Utxo>&           utxos() const { return utxos_; }

    // ----- Snapshot / Restore (Memento) ------------------------------
    LedgerSnapshot createSnapshot(std::string label = "") const {
        LedgerSnapshot s;
        s.label_ = std::move(label);
        s.blockHeight_ = blockHeight_;
        s.accounts_ = accounts_;
        s.contracts_ = contracts_;
        s.utxos_ = utxos_;
        return s;
    }

    void restore(const LedgerSnapshot& s) {
        blockHeight_ = s.blockHeight_;
        accounts_ = s.accounts_;
        contracts_ = s.contracts_;
        utxos_ = s.utxos_;
    }

private:
    std::string                           chainName_;
    std::uint64_t                         blockHeight_ = 0;
    std::map<Address, Account>            accounts_;
    std::map<Address, SmartContractState> contracts_;
    std::map<std::string, Utxo>           utxos_;
};

} // namespace cc
