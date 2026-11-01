#include "cc/testing/Sandbox.h"

#include <stdexcept>

namespace cc {

const char* toString(BridgeKind k) {
    switch (k) {
        case BridgeKind::Token:   return "TokenBridge";
        case BridgeKind::Lending: return "LendingBridge";
        case BridgeKind::Unlock:  return "UnlockBridge";
    }
    return "?";
}

std::vector<StateChange> diffState(const StateView& before, const StateView& after) {
    std::vector<StateChange> out;
    for (const auto& [k, v] : before) {
        auto it = after.find(k);
        if (it == after.end()) out.push_back({k, v, ""});
        else if (it->second != v) out.push_back({k, v, it->second});
    }
    for (const auto& [k, v] : after)
        if (!before.count(k)) out.push_back({k, "", v});
    return out;
}

// ---------------------------------------------------------------------
//  Dựng thế giới
// ---------------------------------------------------------------------
void Sandbox::addLedgerChain(const std::string& name, ChainType type) {
    if (chains_.count(name)) throw std::invalid_argument("Sandbox: chain đã tồn tại: " + name);
    Chain c;
    c.type = type;
    c.ledger = std::make_shared<LocalLedgerSimulator>(name);
    if (type == ChainType::EVM) c.ledgerAdapter = std::make_shared<EVMAdapter>(c.ledger);
    else if (type == ChainType::UTXO) c.ledgerAdapter = std::make_shared<UtxoAdapter>(c.ledger);
    else c.ledgerAdapter = std::make_shared<SolanaAdapter>(c.ledger);
    c.adapter = c.ledgerAdapter;
    c.adapter->listenEvent("*", [this, name](const Event& e) { onChainEvent(name, e); });
    chains_.emplace(name, std::move(c));
}

void Sandbox::addEvmChain(const std::string& name)    { addLedgerChain(name, ChainType::EVM); }
void Sandbox::addUtxoChain(const std::string& name)   { addLedgerChain(name, ChainType::UTXO); }
void Sandbox::addSolanaChain(const std::string& name) { addLedgerChain(name, ChainType::SOLANA); }

void Sandbox::addMockChain(const std::string& name) {
    if (chains_.count(name)) throw std::invalid_argument("Sandbox: chain đã tồn tại: " + name);
    Chain c;
    c.type = ChainType::MOCK;
    c.mock = std::make_shared<MockChainAdapter>(name);
    c.adapter = c.mock;
    c.adapter->listenEvent("*", [this, name](const Event& e) { onChainEvent(name, e); });
    chains_.emplace(name, std::move(c));
}

void Sandbox::connect(const std::string& src, const std::string& dst, RouteOptions opt) {
    if (routes_.count(routeKey(src, dst))) throw std::invalid_argument("Sandbox: tuyến đã tồn tại: " + routeKey(src, dst));
    Chain& s = chain(src);
    Chain& d = chain(dst);

    // Tài khoản gửi lệnh "unlock" khi hoàn tiền phải đúng mẫu địa chỉ của chain nguồn.
    Address op = "operator";
    if (s.type == ChainType::EVM) op = "0x0e7a";
    else if (s.type == ChainType::SOLANA) op = "Refunder1" + std::string(23, '1');
    else if (s.type == ChainType::UTXO) op = "utxo_operator";

    Route r;
    r.kind = opt.kind;
    r.validator = std::make_shared<MultiSigProofValidator>(opt.validators, opt.threshold);
    switch (opt.kind) {
        case BridgeKind::Token:   r.bridge = std::make_shared<TokenBridge>(s.adapter, d.adapter, r.validator); break;
        case BridgeKind::Lending: r.bridge = std::make_shared<LendingBridge>(s.adapter, d.adapter, r.validator); break;
        case BridgeKind::Unlock:  r.bridge = std::make_shared<UnlockBridge>(s.adapter, d.adapter, r.validator); break;
    }
    r.relayer = std::make_unique<CrossChainRelayer>(
        r.bridge, s.adapter, RelayerConfig{opt.maxAttempts, opt.refundOnFailure, op, opt.eventName});
    r.relayer->setObserver([this, src, dst](const RelayRecord& rec, const std::string& stage, bool ok,
                                            const std::string& detail) { onRelayStep(src, dst, rec, stage, ok, detail); });
    r.relayer->start();
    routes_.emplace(routeKey(src, dst), std::move(r));
}

Sandbox::Chain& Sandbox::chain(const std::string& name) {
    auto it = chains_.find(name);
    if (it == chains_.end()) throw std::invalid_argument("Sandbox: không có chain " + name);
    return it->second;
}
const Sandbox::Chain& Sandbox::chain(const std::string& name) const {
    auto it = chains_.find(name);
    if (it == chains_.end()) throw std::invalid_argument("Sandbox: không có chain " + name);
    return it->second;
}
Sandbox::Route& Sandbox::route(const std::string& src, const std::string& dst) {
    auto it = routes_.find(routeKey(src, dst));
    if (it == routes_.end()) throw std::invalid_argument("Sandbox: chưa nối tuyến " + routeKey(src, dst));
    return it->second;
}
const Sandbox::Route& Sandbox::route(const std::string& src, const std::string& dst) const {
    auto it = routes_.find(routeKey(src, dst));
    if (it == routes_.end()) throw std::invalid_argument("Sandbox: chưa nối tuyến " + routeKey(src, dst));
    return it->second;
}

// ---------------------------------------------------------------------
//  ARRANGE
// ---------------------------------------------------------------------
void Sandbox::setBalance(const std::string& name, const Address& who, Amount amount) {
    Chain& c = chain(name);
    if (c.mock) { c.mock->setBalance(who, amount); return; }
    if (c.type == ChainType::UTXO) throw std::invalid_argument("Sandbox: chain UTXO dùng giveUtxo(), không dùng setBalance()");
    Account a = c.ledger->getAccount(who).value_or(Account{who, 0, 0, false});
    a.balance = amount;
    c.ledger->upsertAccount(a);
}

std::string Sandbox::giveUtxo(const std::string& name, const Address& owner, Amount amount) {
    Chain& c = chain(name);
    if (c.type != ChainType::UTXO) throw std::invalid_argument("Sandbox: giveUtxo() chỉ dùng cho chain UTXO");
    std::string id = "genesis" + std::to_string(++utxoSeq_) + ":0";
    c.ledger->addUtxo({id, owner, amount, false});
    return id;
}

void Sandbox::setStorage(const std::string& name, const Address& contract, const std::string& slot, const std::string& value) {
    Chain& c = chain(name);
    if (!c.ledger) throw std::invalid_argument("Sandbox: chain Mock không có storage hợp đồng");
    SmartContractState st = c.ledger->getContract(contract).value_or(SmartContractState{contract, name, "", {}});
    st.set(slot, value);
    c.ledger->upsertContract(st);
}

void Sandbox::failNext(const std::string& name, TxStatus status, const std::string& reason, int count) {
    Chain& c = chain(name);
    if (!c.mock) throw std::invalid_argument("Sandbox: failNext() chỉ dùng cho chain Mock");
    c.mock->injectFailure(status, reason, count);
}

void Sandbox::setActiveSigners(const std::string& src, const std::string& dst, std::vector<Address> signers) {
    route(src, dst).validator->setActiveSigners(std::move(signers));
}

// ---------------------------------------------------------------------
//  ACT
// ---------------------------------------------------------------------
TransactionResult Sandbox::lock(const std::string& src, const std::string& dst,
                                const Address& from, Amount amount, const Address& receiver, const std::string& memo) {
    KeyValue args{{"receiver", receiver}, {"targetChain", dst}};
    if (!memo.empty()) args["memo"] = memo;
    // Ghi bước của người dùng TRƯỚC (sự kiện Locked phát ra trong lúc giao dịch chạy), cập nhật kết quả sau.
    const std::size_t step = trace_.size();
    recordFlow(from, src, "lock " + amountToString(amount) + (memo.empty() ? "" : " [" + memo + "]"), "info");
    TransactionResult r = chain(src).adapter->sendTransaction({"lock", from, "", amount, args});
    trace_[step].status = r.ok() ? "ok" : "fail";
    trace_[step].detail = r.revertReason;
    return r;
}

std::size_t Sandbox::relayAll() {
    std::size_t total = 0;
    // Lặp tới khi mọi relayer hết thư (một relayer có thể sinh việc cho relayer khác).
    for (int round = 0; round < 20; ++round) {
        std::size_t n = 0;
        for (auto& [key, r] : routes_) n += r.relayer->runUntilIdle();
        total += n;
        if (n == 0) break;
    }
    return total;
}

TransactionResult Sandbox::send(const std::string& name, const TxPayload& payload) {
    const std::size_t step = trace_.size();
    recordFlow(payload.from.empty() ? "Hệ thống" : payload.from, name,
               payload.action + " " + amountToString(payload.amount) + (payload.to.empty() ? "" : " → " + payload.to), "info");
    TransactionResult r = chain(name).adapter->sendTransaction(payload);
    trace_[step].status = r.ok() ? "ok" : "fail";
    trace_[step].detail = r.revertReason;
    return r;
}

TransactionResult Sandbox::submitRaw(const std::string& name, const ITransaction& tx) {
    Chain& c = chain(name);
    if (!c.ledgerAdapter) throw std::invalid_argument("Sandbox: chain Mock không nhận giao dịch dựng sẵn");
    const std::size_t step = trace_.size();
    recordFlow("Người dùng", name, "giao dịch dựng sẵn " + tx.hash(), "info");
    TransactionResult r = c.ledgerAdapter->submit(tx);
    trace_[step].status = r.ok() ? "ok" : "fail";
    trace_[step].detail = r.revertReason;
    return r;
}

TransactionResult Sandbox::injectMessage(const std::string& src, const std::string& dst, CrossChainMessage msg) {
    Route& rt = route(src, dst);
    const std::string reason = rt.bridge->verify(msg);
    recordFlow("Thư dựng tay", "Validator", "kiểm thư #" + std::to_string(msg.nonce) + " (" + amountToString(msg.amount) + ")",
               reason.empty() ? "ok" : "fail", reason);
    TransactionResult r = rt.bridge->relay(msg);
    if (reason.empty())
        recordFlow("Relayer", dst, std::string(toString(rt.kind)) + " → " + msg.receiver, r.ok() ? "ok" : "fail", r.revertReason);
    return r;
}

CrossChainMessage Sandbox::makeMessage(const std::string& src, const std::string& dst, const Address& sender,
                                       const Address& receiver, Amount amount, std::uint64_t nonce) const {
    CrossChainMessage m;
    m.sourceChain = src;
    m.targetChain = dst;
    m.sender = sender;
    m.receiver = receiver;
    m.amount = amount;
    m.nonce = nonce;
    m.messageHash = m.computeHash();
    m.proof = route(src, dst).validator->createProof(m);
    return m;
}

// ---------------------------------------------------------------------
//  Đọc trạng thái
// ---------------------------------------------------------------------
Amount Sandbox::balance(const std::string& name, const Address& who) const { return chain(name).adapter->getBalance(who); }

const std::vector<RelayRecord>& Sandbox::relayRecords(const std::string& src, const std::string& dst) const {
    return route(src, dst).relayer->records();
}

std::vector<RouteRelay> Sandbox::allRelayRecords() const {
    std::vector<RouteRelay> out;
    for (const auto& [key, r] : routes_)
        for (const auto& rec : r.relayer->records())
            out.push_back({r.bridge->source().chainName(), r.bridge->target().chainName(), rec});
    return out;
}

std::shared_ptr<IBlockchainAdapter> Sandbox::adapter(const std::string& name) const { return chain(name).adapter; }
std::shared_ptr<LocalLedgerSimulator> Sandbox::ledger(const std::string& name) const { return chain(name).ledger; }

std::vector<std::string> Sandbox::chainNames() const {
    std::vector<std::string> out;
    for (const auto& [n, c] : chains_) out.push_back(n);
    return out;
}

std::vector<std::pair<std::string, std::string>> Sandbox::routes() const {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& [k, r] : routes_) out.emplace_back(r.bridge->source().chainName(), r.bridge->target().chainName());
    return out;
}

StateView Sandbox::stateView() const {
    StateView v;
    for (const auto& [name, c] : chains_) {
        if (c.mock) {
            for (const auto& [a, bal] : c.mock->balances())
                if (bal != 0) v[name + "/account/" + a + "/balance"] = amountToString(bal);
            continue;
        }
        for (const auto& [a, acc] : c.ledger->accounts()) {
            v[name + "/account/" + a + "/balance"] = amountToString(acc.balance);
            if (acc.nonce) v[name + "/account/" + a + "/nonce"] = std::to_string(acc.nonce);
        }
        for (const auto& [addr, st] : c.ledger->contracts())
            for (const auto& [slot, val] : st.storage) v[name + "/storage/" + addr + "/" + slot] = val;
        for (const auto& [id, u] : c.ledger->utxos())
            v[name + "/utxo/" + id] = u.owner + " " + amountToString(u.amount) + (u.spent ? " (đã tiêu)" : "");
    }
    for (const auto& [key, r] : routes_) {
        const auto& recs = r.relayer->records();
        for (std::size_t i = 0; i < recs.size(); ++i)
            v[key + "/relay/" + std::to_string(i)] = std::string(toString(recs[i].status)) + " sau " +
                                                     std::to_string(recs[i].attempts) + " lần";
    }
    return v;
}

// ---------------------------------------------------------------------
//  FLOW TRACE
// ---------------------------------------------------------------------
void Sandbox::recordFlow(const std::string& from, const std::string& to, const std::string& label,
                         const std::string& status, const std::string& detail) {
    trace_.push_back({trace_.size() + 1, from, to, label, status, detail});
}

void Sandbox::onChainEvent(const std::string& name, const Event& e) {
    // Chỉ ghi các sự kiện kích hoạt relayer (thư xuyên chuỗi được sinh ra từ đây).
    if (e.name != "Locked" && e.name != "CollateralReleased") return;
    auto f = [&](const std::string& k) { auto it = e.fields.find(k); return it == e.fields.end() ? std::string() : it->second; };
    recordFlow(name, "Relayer", "sự kiện " + e.name + (f("nonce").empty() ? "" : " #" + f("nonce")) + " (" + f("amount") + ")",
               "info", f("reason"));
}

void Sandbox::onRelayStep(const std::string& src, const std::string& dst, const RelayRecord& r,
                          const std::string& stage, bool ok, const std::string& detail) {
    const auto& m = r.message;
    const std::string n = "#" + std::to_string(m.nonce);
    if (stage == "verify") {
        recordFlow("Relayer", "Validator", "kiểm chữ ký thư " + n, ok ? "ok" : "fail", detail);
    } else if (stage == "execute") {
        std::string label;
        switch (route(src, dst).kind) {
            case BridgeKind::Token:   label = "mint " + amountToString(m.amount) + " → " + m.receiver; break;
            case BridgeKind::Unlock:  label = "unlock " + amountToString(m.amount) + " → " + m.receiver; break;
            case BridgeKind::Lending: {
                auto memo = m.payload.find("memo");
                label = "borrow " + amountToString(LendingBridge::borrowAmountFromMemo(memo == m.payload.end() ? "" : memo->second)) +
                        " (thế chấp " + amountToString(m.amount) + ") → " + m.receiver;
                break;
            }
        }
        recordFlow("Relayer", dst, label + " (lần " + std::to_string(r.attempts) + ")", ok ? "ok" : "fail", detail);
    } else if (stage == "retry") {
        recordFlow("Relayer", "Relayer", "chờ thử lại thư " + n, "info", detail);
    } else if (stage == "refund") {
        recordFlow("Relayer", src, "hoàn " + amountToString(m.amount) + " cho " + m.sender, ok ? "ok" : "fail", detail);
    }
}

// ---------------------------------------------------------------------
//  ASSERT — ghi lại, không dừng
// ---------------------------------------------------------------------
void Sandbox::record(const std::string& d, const std::string& e, const std::string& a) {
    assertions_.push_back({d, e, a, e == a});
}

void Sandbox::expectBalance(const std::string& name, const Address& who, Amount expected) {
    record("số dư " + who + " ở " + name, amountToString(expected), amountToString(balance(name, who)));
}

void Sandbox::expectRelayCount(const std::string& src, const std::string& dst, std::size_t expected) {
    record("số thư trên tuyến " + routeKey(src, dst), std::to_string(expected),
           std::to_string(relayRecords(src, dst).size()));
}

void Sandbox::expectRelayStatus(const std::string& src, const std::string& dst, std::size_t index, RelayStatus expected) {
    const auto& recs = relayRecords(src, dst);
    std::string actual = index < recs.size() ? toString(recs[index].status) : "(không có thư)";
    record("trạng thái thư #" + std::to_string(index) + " tuyến " + routeKey(src, dst), toString(expected), actual);
}

void Sandbox::expectStorage(const std::string& name, const Address& contract, const std::string& slot, const std::string& expected) {
    auto v = chain(name).adapter->getContractState(contract).get(slot);
    record(contract + "." + slot + " ở " + name, expected, v ? *v : "(trống)");
}

void Sandbox::expectTxStatus(const TransactionResult& r, TxStatus expected, const std::string& what) {
    std::string actual = toString(r.status);
    assertions_.push_back({what, toString(expected),
                           r.revertReason.empty() ? actual : actual + " — " + r.revertReason,
                           r.status == expected});
}

void Sandbox::expectTrue(bool condition, const std::string& description) {
    assertions_.push_back({description, "đúng", condition ? "đúng" : "sai", condition});
}

bool Sandbox::allPassed() const {
    for (const auto& a : assertions_)
        if (!a.passed) return false;
    return true;
}

// ---------------------------------------------------------------------
//  SNAPSHOT & REVERTSTATE
// ---------------------------------------------------------------------
SandboxSnapshot Sandbox::snapshot(const std::string& label) const {
    SandboxSnapshot s;
    s.label = label;
    for (const auto& [name, c] : chains_) {
        if (c.ledger) {
            s.ledgers.emplace(name, c.ledger->createSnapshot(label));
            s.engines.emplace(name, c.ledgerAdapter->engine().saveState());
        }
        if (c.mock) s.mocks.emplace(name, c.mock->saveState());
    }
    for (const auto& [key, r] : routes_) {
        s.validators.emplace(key, r.validator->saveState());
        s.relayers.emplace(key, r.relayer->saveState());
    }
    s.view = stateView();
    return s;
}

void Sandbox::revertState(const SandboxSnapshot& s) {
    for (auto& [name, c] : chains_) {
        if (c.ledger) {
            c.ledger->restore(s.ledgers.at(name));
            c.ledgerAdapter->engine().restoreState(s.engines.at(name));
        }
        if (c.mock) c.mock->restoreState(s.mocks.at(name));
    }
    for (auto& [key, r] : routes_) {
        r.validator->restoreState(s.validators.at(key));
        r.relayer->restoreState(s.relayers.at(key));
    }
}

} // namespace cc
