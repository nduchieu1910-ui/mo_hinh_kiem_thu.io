#include "cc/engine/ExecutionEngine.h"

#include "cc/core/HashUtil.h"

#include <utility>

namespace cc {

namespace {

// data EVM = "action|k=v|k=v"  ->  (action, {k: v})
std::pair<std::string, KeyValue> decodeEvmData(const Bytes& data) {
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

std::string get(const KeyValue& kv, const std::string& k) {
    auto it = kv.find(k);
    return it == kv.end() ? std::string() : it->second;
}

bool isAccountAction(const std::string& a) {
    return a == "transfer" || a == "lock" || a == "unlock" || a == "mint" ||
           a == "borrow" || a == "repay" || a == "liquidate";
}

// Đọc một tham số kiểu số của hợp đồng, có giá trị mặc định.
Amount readNumOr(const SmartContractState& st, const std::string& key, Amount fallback) {
    auto v = st.get(key);
    return v ? amountFromString(*v) : fallback;
}

} // namespace

ExecutionEngine::ExecutionEngine(std::shared_ptr<LocalLedgerSimulator> ledger, Address bridgeAccount)
    : ledger_(std::move(ledger)), bridge_(std::move(bridgeAccount)) {
    if (!ledger_) throw std::invalid_argument("ExecutionEngine: thiếu sổ cái");
}

// ---------------------------------------------------------------------
//  Bảng gas giả lập
// ---------------------------------------------------------------------
std::uint64_t ExecutionEngine::evmIntrinsicGas(const std::string& action) {
    if (action == "transfer") return 21000;
    if (action == "lock") return 52000;
    if (action == "unlock") return 48000;
    if (action == "mint") return 45000;
    return 30000;
}

std::uint64_t ExecutionEngine::utxoGas(const UtxoTransaction& tx) {
    return 100 * tx.inputs.size() + 50 * tx.outputs.size();
}

std::uint64_t ExecutionEngine::solanaComputeUnits(const SolanaInstruction& ix) {
    return 300 + 150 * ix.accounts.size() + (ix.programId == "bridge" ? 2000 : 0);
}

TransactionResult ExecutionEngine::reverted(const Hash& h, const std::string& reason) const {
    auto r = TransactionResult::failure(TxStatus::REVERTED, reason, ledger_->chainName());
    r.txHash = h;
    return r;
}

// ---------------------------------------------------------------------
//  Điểm vào chung: chọn cách thực thi theo định dạng giao dịch
// ---------------------------------------------------------------------
TransactionResult ExecutionEngine::execute(const ITransaction& tx) {
    switch (tx.format()) {
        case TxFormat::EVM:                return executeEvm(static_cast<const EvmTransaction&>(tx));
        case TxFormat::UTXO:               return executeUtxo(static_cast<const UtxoTransaction&>(tx));
        case TxFormat::SOLANA_INSTRUCTION: return executeSolana(static_cast<const SolanaInstruction&>(tx));
    }
    return reverted(tx.hash(), "định dạng giao dịch không hỗ trợ");
}

// ---------------------------------------------------------------------
//  Hành động trên mô hình tài khoản (EVM + Solana)
// ---------------------------------------------------------------------
std::vector<Event> ExecutionEngine::applyAccountAction(const std::string& action, const Address& from,
                                                       const Address& to, Amount value,
                                                       const KeyValue& args, const Hash& txHash) {
    auto account = [&](const Address& a) { return ledger_->getAccount(a).value_or(Account{a, 0, 0, false}); };
    auto debit = [&](const Address& a, Amount v) {
        Account x = account(a);
        if (x.balance < v)
            throw RevertError("không đủ tiền: " + a + " có " + amountToString(x.balance) + ", cần " + amountToString(v));
        x.balance -= v;
        ledger_->upsertAccount(x);
    };
    auto credit = [&](const Address& a, Amount v) {
        Account x = account(a);
        x.balance += v;
        if (a == bridge_) x.isContract = true;
        ledger_->upsertAccount(x);
    };

    SmartContractState bridge = ledger_->getContract(bridge_)
                                    .value_or(SmartContractState{bridge_, ledger_->chainName(), "LockBridge", {}});
    std::vector<Event> events;
    const std::string amt = amountToString(value);

    if (action == "transfer") {
        debit(from, value);
        credit(to, value);
        events.push_back({"Transferred", to, {{"from", from}, {"to", to}, {"amount", amt}}});
    } else if (action == "lock") {
        debit(from, value);
        credit(bridge_, value);
        const Amount outNonce = readNum(bridge, "outboundNonce");
        bridge.set("lockedTotal", amountToString(readNum(bridge, "lockedTotal") + value));
        bridge.set("outboundNonce", amountToString(outNonce + 1));
        events.push_back({"Locked", bridge_, {{"sender", from}, {"receiver", get(args, "receiver")}, {"amount", amt},
                                              {"nonce", amountToString(outNonce)}, {"txHash", txHash},
                                              {"targetChain", get(args, "targetChain")}, {"memo", get(args, "memo")}}});
    } else if (action == "unlock") {
        debit(bridge_, value);
        credit(to, value);
        bridge.set("lockedTotal", amountToString(readNum(bridge, "lockedTotal") - value));
        events.push_back({"Unlocked", bridge_, {{"receiver", to}, {"amount", amt}}});
    } else if (action == "mint") {
        credit(to, value);
        bridge.set("totalMinted", amountToString(readNum(bridge, "totalMinted") + value));
        events.push_back({"Minted", bridge_, {{"receiver", to}, {"amount", amt},
                                              {"messageHash", get(args, "messageHash")}}});
    } else if (action == "borrow" || action == "repay" || action == "liquidate") {
        // ----- Hợp đồng LendingPool (dùng chung tài khoản bridge_ làm quỹ cho vay) -----
        //  priceX100 : giá 1 đơn vị thế chấp tính theo tiền vay, nhân 100 (oracle giả lập)
        //  ltvBps    : tỷ lệ cho vay tối đa (5000 = 50%)
        //  liqBps    : ngưỡng thanh lý (8000 = 80%)
        const Amount price = readNumOr(bridge, "priceX100", 100);
        const Amount ltv = readNumOr(bridge, "ltvBps", 5000);
        const Amount liq = readNumOr(bridge, "liqBps", 8000);
        const std::string borrower = action == "repay" ? from : to;
        const std::string kColl = "collateral[" + borrower + "]", kDebt = "debt[" + borrower + "]",
                          kOwner = "owner[" + borrower + "]";
        Amount coll = readNum(bridge, kColl);
        Amount debt = readNum(bridge, kDebt);

        auto releaseCollateral = [&](const std::string& homeReceiver, const std::string& reason) {
            const Amount rel = readNum(bridge, "releaseNonce");
            events.push_back({"CollateralReleased", bridge_, {{"sender", borrower}, {"receiver", homeReceiver},
                {"amount", amountToString(coll)}, {"nonce", amountToString(rel)}, {"txHash", txHash}, {"reason", reason}}});
            bridge.set("releaseNonce", amountToString(rel + 1));
            bridge.set(kColl, "0");
            coll = 0;
        };

        if (action == "borrow") {
            coll += amountFromString(get(args, "collateral").empty() ? "0" : get(args, "collateral"));
            const Amount maxDebt = coll * price * ltv / (100 * 10000);
            if (debt + value > maxDebt)
                throw RevertError("vượt hạn mức vay: nợ sẽ là " + amountToString(debt + value) +
                                  ", tối đa " + amountToString(maxDebt));
            debit(bridge_, value);                     // quỹ cho vay chi tiền
            credit(to, value);
            debt += value;
            bridge.set(kColl, amountToString(coll));
            if (!get(args, "collateralOwner").empty()) bridge.set(kOwner, get(args, "collateralOwner"));
            events.push_back({"Borrowed", bridge_, {{"borrower", to}, {"amount", amt}, {"debt", amountToString(debt)},
                                                    {"maxDebt", amountToString(maxDebt)}}});
        } else if (action == "repay") {
            if (value > debt)
                throw RevertError("trả nhiều hơn số nợ: nợ " + amountToString(debt) + ", trả " + amt);
            debit(from, value);
            credit(bridge_, value);
            debt -= value;
            events.push_back({"Repaid", bridge_, {{"borrower", from}, {"amount", amt}, {"remainingDebt", amountToString(debt)}}});
            if (debt == 0 && coll > 0) {
                auto owner = bridge.get(kOwner);
                releaseCollateral(owner ? *owner : from, "repaid");
            }
        } else {  // liquidate: from = người thanh lý, to = người vay
            const Amount threshold = coll * price * liq / (100 * 10000);
            if (debt == 0 || debt <= threshold)
                throw RevertError("khoản vay còn an toàn, chưa thể thanh lý: nợ " + amountToString(debt) +
                                  ", ngưỡng " + amountToString(threshold));
            if (value < debt) throw RevertError("người thanh lý phải trả đủ nợ " + amountToString(debt));
            debit(from, debt);
            credit(bridge_, debt);
            events.push_back({"Liquidated", bridge_, {{"borrower", to}, {"liquidator", from},
                                                      {"debtPaid", amountToString(debt)}, {"collateral", amountToString(coll)}}});
            debt = 0;
            releaseCollateral(get(args, "receiverOnHome"), "liquidated");
        }
        bridge.set(kDebt, amountToString(debt));
    } else {
        throw RevertError("hành động không hỗ trợ: " + action);
    }
    ledger_->upsertContract(bridge);
    return events;
}

// ---------------------------------------------------------------------
//  EVM
// ---------------------------------------------------------------------
TransactionResult ExecutionEngine::executeEvm(const EvmTransaction& tx) {
    const auto decoded = decodeEvmData(tx.data);
    const std::string& action = decoded.first;
    const KeyValue& args = decoded.second;

    // 1. Chữ ký
    if (tx.signature != simSign(tx.from, tx.txHash)) return reverted(tx.txHash, "chữ ký không hợp lệ");

    // 2. Nonce (chống phát lại)
    Account sender = ledger_->getAccount(tx.from).value_or(Account{tx.from, 0, 0, false});
    if (tx.nonce != sender.nonce)
        return reverted(tx.txHash, "nonce không khớp: cần " + std::to_string(sender.nonce) + ", nhận " +
                                       std::to_string(tx.nonce) + " (giao dịch lặp?)");
    if (!isAccountAction(action)) return reverted(tx.txHash, "hành động không hỗ trợ: " + action);

    // 3. Gas giả lập — hết gas: như EVM thật, vẫn tăng nonce và thu phí, không làm gì khác
    const std::uint64_t gas = evmIntrinsicGas(action);
    if (gas > tx.gasLimit) {
        const Amount fee = tx.gasPrice * tx.gasLimit;
        sender.nonce += 1;
        sender.balance = sender.balance > fee ? sender.balance - fee : Amount(0);
        ledger_->upsertAccount(sender);
        ledger_->mineBlock();
        auto r = reverted(tx.txHash, "hết gas: cần " + std::to_string(gas) + ", giới hạn " + std::to_string(tx.gasLimit));
        r.gasUsed = tx.gasLimit;
        return r;
    }

    // 4. Ghi sổ — chụp trước, gặp Revert thì Rollback
    const LedgerSnapshot before = ledger_->createSnapshot("pre-" + tx.txHash);
    std::vector<Event> events;
    try {
        const Amount fee = tx.gasPrice * gas;
        if (sender.balance < fee) throw RevertError("không đủ tiền trả phí gas");
        sender.nonce += 1;
        sender.balance -= fee;
        ledger_->upsertAccount(sender);
        events = applyAccountAction(action, tx.from, tx.to, tx.value, args, tx.txHash);
        ledger_->mineBlock();
    } catch (const RevertError& e) {
        rollback(before);
        return reverted(tx.txHash, e.what());
    }

    auto r = TransactionResult::success(tx.txHash, ledger_->chainName(), gas);
    r.events = std::move(events);
    return r;
}

// ---------------------------------------------------------------------
//  UTXO
// ---------------------------------------------------------------------
TransactionResult ExecutionEngine::executeUtxo(const UtxoTransaction& tx) {
    if (tx.outputs.empty()) return reverted(tx.txHash, "giao dịch không có tờ ra");

    // 1. Kiểm từng tờ vào + chữ ký (chưa ghi gì)
    std::set<std::string> seen;
    Amount totalIn = 0, totalOut = 0;
    Address firstOwner;
    for (const TxInput& in : tx.inputs) {
        if (!seen.insert(in.utxoId).second) return reverted(tx.txHash, "dùng một tờ hai lần trong cùng giao dịch: " + in.utxoId);
        auto u = ledger_->getUtxo(in.utxoId);
        if (!u) return reverted(tx.txHash, "không tìm thấy tờ: " + in.utxoId);
        if (u->spent) return reverted(tx.txHash, "UTXO đã được tiêu: " + in.utxoId);
        if (in.signature != simSign(u->owner, in.utxoId)) return reverted(tx.txHash, "chữ ký không đúng chủ tờ: " + in.utxoId);
        if (firstOwner.empty()) firstOwner = u->owner;
        totalIn += u->amount;
    }
    for (const TxOutput& out : tx.outputs) {
        if (out.amount == 0) return reverted(tx.txHash, "tờ ra có giá trị 0");
        totalOut += out.amount;
    }
    const bool isMint = tx.inputs.empty();
    if (isMint && get(tx.meta, "action") != "mint") return reverted(tx.txHash, "giao dịch không có tờ vào");
    if (!isMint && totalIn < totalOut) return reverted(tx.txHash, "tổng tờ ra lớn hơn tổng tờ vào");

    // 2. Ghi sổ — chụp trước, lỗi giữa chừng thì Rollback (tờ đã tiêu được trả lại)
    const LedgerSnapshot before = ledger_->createSnapshot("pre-" + tx.txHash);
    try {
        for (const TxInput& in : tx.inputs) ledger_->spendUtxo(in.utxoId);
        for (std::size_t i = 0; i < tx.outputs.size(); ++i)
            ledger_->addUtxo({tx.txHash + ":" + std::to_string(i), tx.outputs[i].owner, tx.outputs[i].amount, false});
        ledger_->mineBlock();
    } catch (const std::exception& e) {
        rollback(before);
        return reverted(tx.txHash, std::string("lỗi khi ghi sổ, đã rollback: ") + e.what());
    }

    std::vector<Event> events;
    const std::string amt = amountToString(tx.outputs[0].amount);
    if (isMint)
        events.push_back({"Minted", "", {{"receiver", tx.outputs[0].owner}, {"amount", amt}, {"messageHash", get(tx.meta, "messageHash")}}});
    else
        events.push_back({"Transferred", "", {{"from", firstOwner}, {"to", tx.outputs[0].owner}, {"amount", amt}}});

    auto r = TransactionResult::success(tx.txHash, ledger_->chainName(), utxoGas(tx));
    r.events = std::move(events);
    return r;
}

// ---------------------------------------------------------------------
//  Solana
// ---------------------------------------------------------------------
TransactionResult ExecutionEngine::executeSolana(const SolanaInstruction& ix) {
    // 1. Chống trùng: cùng một giao dịch (cùng chữ ký) chỉ chạy một lần
    if (processedSolana_.count(ix.txHash)) return reverted(ix.txHash, "giao dịch đã được xử lý (trùng chữ ký)");

    // 2. Chữ ký của người trả phí
    if (ix.signatures.empty() || ix.signatures.front() != simSign(ix.feePayer, ix.txHash))
        return reverted(ix.txHash, "chữ ký không hợp lệ");

    // 3. recentBlockhash còn hạn?
    std::uint64_t bh = 0;
    try {
        if (ix.recentBlockhash.rfind("bh:", 0) != 0) throw std::invalid_argument("x");
        bh = std::stoull(ix.recentBlockhash.substr(3));
    } catch (const std::exception&) {
        return reverted(ix.txHash, "recentBlockhash sai định dạng");
    }
    const std::uint64_t height = ledger_->blockHeight();
    if (bh > height || height - bh > kSolanaBlockhashTtl) return reverted(ix.txHash, "recentBlockhash đã hết hạn");

    // 4. Program có hỗ trợ hành động này không?
    const bool okProgram = (ix.programId == "system" && ix.action == "transfer") ||
                           (ix.programId == "bridge" && (ix.action == "lock" || ix.action == "unlock" || ix.action == "mint")) ||
                           (ix.programId == "lending" && (ix.action == "borrow" || ix.action == "repay" || ix.action == "liquidate"));
    if (!okProgram) return reverted(ix.txHash, "program '" + ix.programId + "' không hỗ trợ '" + ix.action + "'");

    // 5. Compute unit giả lập
    const std::uint64_t cu = solanaComputeUnits(ix);
    const std::uint64_t limit = ix.args.count("computeUnitLimit") ? std::stoull(ix.args.at("computeUnitLimit")) : 200000;
    if (cu > limit) return reverted(ix.txHash, "vượt compute unit: cần " + std::to_string(cu) + ", giới hạn " + std::to_string(limit));

    // 6. Ghi sổ — chụp trước, gặp Revert thì Rollback
    const LedgerSnapshot before = ledger_->createSnapshot("pre-" + ix.txHash);
    std::vector<Event> events;
    try {
        const Amount fee = lamportsPerSig_ * ix.signatures.size();
        if (fee > 0) {
            Account payer = ledger_->getAccount(ix.feePayer).value_or(Account{ix.feePayer, 0, 0, false});
            if (payer.balance < fee) throw RevertError("không đủ lamports trả phí");
            payer.balance -= fee;
            ledger_->upsertAccount(payer);
        }
        events = applyAccountAction(ix.action, ix.feePayer, ix.to, ix.amount, ix.args, ix.txHash);
        ledger_->mineBlock();
    } catch (const RevertError& e) {
        rollback(before);
        return reverted(ix.txHash, e.what());
    }

    processedSolana_.insert(ix.txHash);
    auto r = TransactionResult::success(ix.txHash, ledger_->chainName(), cu);
    r.events = std::move(events);
    return r;
}

} // namespace cc
