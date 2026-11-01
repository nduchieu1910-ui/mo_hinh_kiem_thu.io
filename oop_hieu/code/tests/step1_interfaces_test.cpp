// =====================================================================
//  Bước 1 — kiểm tra các interface lõi và dữ liệu dùng chung ăn khớp.
//  Dùng adapter / validator giả (fake) — adapter thật làm ở Bước 2.
// =====================================================================
#include "cc/adapter/IBlockchainAdapter.h"
#include "cc/bridge/CrossChainBridge.h"
#include "cc/ledger/LocalLedgerSimulator.h"

#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>

using namespace cc;

static int failures = 0;
#define CHECK(cond, name)                                                   \
    do {                                                                    \
        if (cond) std::cout << "  [PASS] " << name << "\n";                 \
        else { std::cout << "  [FAIL] " << name << "\n"; ++failures; }      \
    } while (0)

// Adapter giả: ghi trực tiếp vào LocalLedgerSimulator (mô hình UTXO).
class FakeUtxoAdapter : public IBlockchainAdapter {
public:
    explicit FakeUtxoAdapter(std::shared_ptr<LocalLedgerSimulator> l) : ledger_(std::move(l)) {}
    ChainType   chainType() const override { return ChainType::MOCK; }
    std::string chainName() const override { return ledger_->chainName(); }
    Amount getBalance(const Address& a) const override { return ledger_->utxoBalanceOf(a); }

    TransactionResult sendTransaction(const TxPayload& p) override {
        if (p.action != "mint" || p.amount == 0)
            return TransactionResult::failure(TxStatus::REVERTED, "payload không hợp lệ", chainName());
        std::string id = "tx" + std::to_string(++counter_) + ":0";
        ledger_->addUtxo({id, p.to, p.amount, false});
        ledger_->mineBlock();
        return TransactionResult::success(id, chainName(), 21000);
    }

    SmartContractState getContractState(const Address& c) const override {
        return ledger_->getContract(c).value_or(SmartContractState{c, chainName(), "", {}});
    }
    void listenEvent(const std::string&, EventCallback) override {}

private:
    std::shared_ptr<LocalLedgerSimulator> ledger_;
    int counter_ = 0;
};

// Validator giả: con dấu = "SIGNED:" + hash; mỗi hash chỉ dùng một lần.
class FakeProofValidator : public IProofValidator {
public:
    std::string validate(const CrossChainMessage& m) override {
        if (m.proof != "SIGNED:" + m.messageHash) return "con dấu không hợp lệ";
        if (!used_.insert(m.messageHash).second)  return "thư đã được xử lý (replay)";
        return "";
    }
private:
    std::set<Hash> used_;
};

static CrossChainMessage makeMessage(std::uint64_t nonce, Amount amount) {
    CrossChainMessage m;
    m.sourceChain = "ChainA-EVM";
    m.targetChain = "ChainB-UTXO";
    m.sender = "0xA11CE";
    m.receiver = "alice_b";
    m.nonce = nonce;
    m.amount = amount;
    m.payload["token"] = "0x70KEN";
    m.messageHash = m.computeHash();
    m.proof = "SIGNED:" + m.messageHash;
    return m;
}

int main() {
    auto ledgerA = std::make_shared<LocalLedgerSimulator>("ChainA-EVM");
    auto ledgerB = std::make_shared<LocalLedgerSimulator>("ChainB-UTXO");
    auto adapterA = std::make_shared<FakeUtxoAdapter>(ledgerA);
    auto adapterB = std::make_shared<FakeUtxoAdapter>(ledgerB);
    auto validator = std::make_shared<FakeProofValidator>();
    TokenBridge bridge(adapterA, adapterB, validator);

    std::cout << "TC01 - chuyen thanh cong\n";
    auto msg = makeMessage(0, 100);
    auto r1 = bridge.relay(msg);
    CHECK(r1.ok(), "relay tra ve SUCCESS");
    CHECK(msg.status == MessageStatus::EXECUTED, "thu o trang thai EXECUTED");
    CHECK(adapterB->getBalance("alice_b") == 100, "alice_b co 100 o ChainB");

    std::cout << "TC02 - gui lai thu cu\n";
    auto again = msg;
    auto r2 = bridge.relay(again);
    CHECK(r2.status == TxStatus::FAILED, "relay lan 2 bi tu choi");
    CHECK(again.status == MessageStatus::FAILED, "thu lap o trang thai FAILED");
    CHECK(adapterB->getBalance("alice_b") == 100, "so du van la 100");

    std::cout << "Thu gia\n";
    auto fake = makeMessage(1, 1000000);
    fake.proof = "FORGED";
    CHECK(bridge.relay(fake).status == TxStatus::FAILED, "thu gia bi chan");

    std::cout << "Snapshot / Restore (Memento)\n";
    auto snap = ledgerB->createSnapshot("before-act");
    ledgerB->addUtxo({"extra:0", "alice_b", 50, false});
    CHECK(ledgerB->utxoBalanceOf("alice_b") == 150, "sau khi them: 150");
    ledgerB->restore(snap);
    CHECK(ledgerB->utxoBalanceOf("alice_b") == 100, "sau khi restore: 100");

    std::cout << "Chong tieu 2 lan (double-spend)\n";
    ledgerB->spendUtxo("tx1:0");
    bool threw = false;
    try { ledgerB->spendUtxo("tx1:0"); } catch (const std::logic_error&) { threw = true; }
    CHECK(threw, "tieu lan 2 bi tu choi");

    std::cout << (failures == 0 ? "\nTat ca test DAT\n" : "\nCo test KHONG DAT\n");
    return failures == 0 ? 0 : 1;
}
