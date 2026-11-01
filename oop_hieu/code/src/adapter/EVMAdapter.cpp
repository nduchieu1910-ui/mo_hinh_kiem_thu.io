#include "cc/adapter/EVMAdapter.h"

#include "cc/factory/EVMTransactionFactory.h"

#include <utility>

namespace cc {

namespace {

// data = "action|k=v|k=v"  ->  (action, {k: v})
std::pair<std::string, KeyValue> decode(const Bytes& data) {
    std::string s(data.begin(), data.end());
    std::pair<std::string, KeyValue> out;
    std::size_t start = 0;
    bool first = true;
    while (start <= s.size()) {
        std::size_t end = s.find('|', start);
        if (end == std::string::npos) end = s.size();
        std::string part = s.substr(start, end - start);
        if (first) { out.first = part; first = false; }
        else if (auto eq = part.find('='); eq != std::string::npos)
            out.second[part.substr(0, eq)] = part.substr(eq + 1);
        start = end + 1;
    }
    return out;
}

Amount readNum(const SmartContractState& st, const std::string& key) {
    auto v = st.get(key);
    return v ? amountFromString(*v) : Amount(0);
}

} // namespace

EVMAdapter::EVMAdapter(std::shared_ptr<LocalLedgerSimulator> ledger, Address escrow,
                       std::unique_ptr<TransactionFactory> factory)
    : ledger_(std::move(ledger)), escrow_(std::move(escrow)), factory_(std::move(factory)) {
    if (!ledger_) throw std::invalid_argument("EVMAdapter: thiếu sổ cái");
    if (!factory_) factory_ = std::make_unique<EVMTransactionFactory>(ledger_);
    if (factory_->chainType() != ChainType::EVM)
        throw std::invalid_argument("EVMAdapter: factory không phải loại EVM");
}

std::uint64_t EVMAdapter::intrinsicGas(const std::string& action) {
    if (action == "transfer") return 21000;
    if (action == "lock") return 52000;
    if (action == "unlock") return 48000;
    if (action == "mint") return 45000;
    return 30000;
}

Amount EVMAdapter::getBalance(const Address& address) const {
    return ledger_->balanceOf(address);
}

SmartContractState EVMAdapter::getContractState(const Address& contract) const {
    return ledger_->getContract(contract).value_or(SmartContractState{contract, chainName(), "", {}});
}

TransactionResult EVMAdapter::sendTransaction(const TxPayload& payload) {
    try {
        auto tx = factory_->createTransaction(payload);   // Factory dựng phiếu EVM
        auto* evm = dynamic_cast<EvmTransaction*>(tx.get());
        if (!evm) return TransactionResult::failure(TxStatus::FAILED, "factory trả sai loại giao dịch", chainName());
        return execute(*evm);
    } catch (const TxBuildError& e) {
        return TransactionResult::failure(TxStatus::FAILED, e.what(), chainName());
    }
}

TransactionResult EVMAdapter::execute(const EvmTransaction& tx) {
    auto fail = [&](TxStatus st, const std::string& reason) {
        auto r = TransactionResult::failure(st, reason, chainName());
        r.txHash = tx.txHash;
        return r;
    };

    const auto decoded = decode(tx.data);
    const std::string& action = decoded.first;
    const KeyValue& args = decoded.second;
    Account sender = ledger_->getAccount(tx.from).value_or(Account{tx.from, 0, 0, false});

    // 1. Chống phát lại: nonce phải đúng bằng số giao dịch đã gửi.
    if (tx.nonce != sender.nonce)
        return fail(TxStatus::REVERTED, "nonce không khớp: cần " + std::to_string(sender.nonce) +
                                        ", nhận " + std::to_string(tx.nonce) + " (giao dịch lặp?)");

    // 2. Đủ tiền trả số tiền chuyển + phí tối đa? (mint không trừ tiền người gửi)
    const Amount maxFee = tx.gasPrice * tx.gasLimit;
    const Amount need = maxFee + (action == "mint" ? Amount(0) : tx.value);
    if (sender.balance < need)
        return fail(TxStatus::REVERTED, "không đủ tiền: có " + amountToString(sender.balance) +
                                        ", cần " + amountToString(need));

    if (action != "transfer" && action != "lock" && action != "unlock" && action != "mint")
        return fail(TxStatus::REVERTED, "hành động không hỗ trợ: " + action);

    // 3. Hết gas: giống EVM thật — vẫn tăng nonce và thu phí, nhưng không làm gì khác.
    const std::uint64_t gas = intrinsicGas(action);
    if (gas > tx.gasLimit) {
        sender.nonce += 1;
        sender.balance -= maxFee;
        ledger_->upsertAccount(sender);
        ledger_->mineBlock();
        auto r = fail(TxStatus::REVERTED, "hết gas: cần " + std::to_string(gas) +
                                          ", giới hạn " + std::to_string(tx.gasLimit));
        r.gasUsed = tx.gasLimit;
        return r;
    }

    if (action == "unlock" && ledger_->balanceOf(escrow_) < tx.value)
        return fail(TxStatus::REVERTED, "két bridge không đủ tiền để mở khóa");

    // 4. Ghi vào sổ (đã kiểm hết điều kiện ở trên nên không thể hỏng giữa chừng).
    sender.nonce += 1;
    sender.balance -= tx.gasPrice * gas;
    if (action != "mint") sender.balance -= tx.value;
    ledger_->upsertAccount(sender);

    auto credit = [&](const Address& who, Amount v) {
        Account a = ledger_->getAccount(who).value_or(Account{who, 0, 0, false});
        a.balance += v;
        ledger_->upsertAccount(a);
    };
    auto debitEscrow = [&](Amount v) {
        Account a = ledger_->getAccount(escrow_).value_or(Account{escrow_, 0, 0, true});
        a.balance -= v;
        ledger_->upsertAccount(a);
    };

    SmartContractState bridge = ledger_->getContract(escrow_)
                                    .value_or(SmartContractState{escrow_, chainName(), "LockBridge", {}});
    std::vector<Event> events;
    auto field = [&](const std::string& k) { auto it = args.find(k); return it == args.end() ? std::string() : it->second; };

    if (action == "transfer") {
        credit(tx.to, tx.value);
        events.push_back({"Transferred", tx.to, {{"from", tx.from}, {"to", tx.to}, {"amount", amountToString(tx.value)}}});
    } else if (action == "lock") {
        Account esc = ledger_->getAccount(escrow_).value_or(Account{escrow_, 0, 0, true});
        esc.balance += tx.value;
        esc.isContract = true;
        ledger_->upsertAccount(esc);
        const Amount outNonce = readNum(bridge, "outboundNonce");
        bridge.set("lockedTotal", amountToString(readNum(bridge, "lockedTotal") + tx.value));
        bridge.set("outboundNonce", amountToString(outNonce + 1));
        events.push_back({"Locked", escrow_, {{"sender", tx.from}, {"receiver", field("receiver")},
                                              {"amount", amountToString(tx.value)},
                                              {"nonce", amountToString(outNonce)}, {"txHash", tx.txHash}}});
    } else if (action == "unlock") {
        debitEscrow(tx.value);
        credit(tx.to, tx.value);
        bridge.set("lockedTotal", amountToString(readNum(bridge, "lockedTotal") - tx.value));
        events.push_back({"Unlocked", escrow_, {{"receiver", tx.to}, {"amount", amountToString(tx.value)}}});
    } else {  // mint
        credit(tx.to, tx.value);
        bridge.set("totalMinted", amountToString(readNum(bridge, "totalMinted") + tx.value));
        events.push_back({"Minted", escrow_, {{"receiver", tx.to}, {"amount", amountToString(tx.value)},
                                              {"messageHash", field("messageHash")}}});
    }
    ledger_->upsertContract(bridge);
    ledger_->mineBlock();

    auto r = TransactionResult::success(tx.txHash, chainName(), gas);
    r.events = events;
    emit(events);   // báo cho người đang nghe (vd: Relayer ở Bước 3)
    return r;
}

} // namespace cc
