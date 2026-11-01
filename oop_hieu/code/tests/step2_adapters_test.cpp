// =====================================================================
//  Bước 2 — kiểm tra Factory + Adapter thật (chạy trên Local Memory State)
//  và chạy lại 7 trường hợp của Bước 1 với adapter thật.
// =====================================================================
#include "cc/adapter/EVMAdapter.h"
#include "cc/adapter/MockChainAdapter.h"
#include "cc/adapter/UtxoAdapter.h"
#include "cc/bridge/CrossChainBridge.h"
#include "cc/factory/EVMTransactionFactory.h"
#include "cc/factory/UtxoTransactionFactory.h"
#include "cc/ledger/LocalLedgerSimulator.h"

#include <iostream>
#include <memory>
#include <optional>
#include <set>

using namespace cc;

static int failures = 0, passes = 0;
#define CHECK(cond, name)                                                         \
    do {                                                                          \
        if (cond) { std::cout << "  [PASS] " << name << "\n"; ++passes; }         \
        else { std::cout << "  [FAIL] " << name << "\n"; ++failures; }            \
    } while (0)

static bool contains(const std::string& s, const std::string& part) {
    return s.find(part) != std::string::npos;
}

// Validator tạm cho test (Bước 3 sẽ cài bản chính thức).
class TestProofValidator : public IProofValidator {
public:
    std::string validate(const CrossChainMessage& m) override {
        if (m.proof != "SIGNED:" + m.messageHash) return "con dau khong hop le";
        if (!used_.insert(m.messageHash).second)  return "thu da duoc xu ly (replay)";
        return "";
    }
private:
    std::set<Hash> used_;
};

// Dựng lá thư từ sự kiện "Locked" (Bước 3 sẽ để Relayer tự làm việc này).
static CrossChainMessage messageFromLocked(const Event& e, const std::string& src, const std::string& dst) {
    CrossChainMessage m;
    m.sourceChain = src;
    m.targetChain = dst;
    m.sender = e.fields.at("sender");
    m.receiver = e.fields.at("receiver");
    m.amount = amountFromString(e.fields.at("amount"));
    m.nonce = std::stoull(e.fields.at("nonce"));
    m.messageHash = m.computeHash();
    m.proof = "SIGNED:" + m.messageHash;
    return m;
}

static CrossChainMessage signedMessage(const std::string& receiver, Amount amount, std::uint64_t nonce,
                                       const std::string& dst = "ChainB-UTXO") {
    CrossChainMessage m;
    m.sourceChain = "ChainA-EVM";
    m.targetChain = dst;
    m.sender = "0xa11ce";
    m.receiver = receiver;
    m.amount = amount;
    m.nonce = nonce;
    m.messageHash = m.computeHash();
    m.proof = "SIGNED:" + m.messageHash;
    return m;
}

int main() {
    auto ledgerA = std::make_shared<LocalLedgerSimulator>("ChainA-EVM");
    auto ledgerB = std::make_shared<LocalLedgerSimulator>("ChainB-UTXO");
    ledgerA->upsertAccount({"0xa11ce", 1000, 0, false});

    // ------------------------------------------------------------------
    std::cout << "A. TransactionFactory\n";
    auto fEvm = TransactionFactory::forChain(ChainType::EVM, ledgerA);
    auto fUtxo = TransactionFactory::forChain(ChainType::UTXO, ledgerB);
    CHECK(fEvm->chainType() == ChainType::EVM, "forChain(EVM) tra ve EVMTransactionFactory");
    CHECK(fUtxo->chainType() == ChainType::UTXO, "forChain(UTXO) tra ve UtxoTransactionFactory");
    bool threw = false;
    try { TransactionFactory::forChain(ChainType::MOCK, ledgerA); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw, "forChain(MOCK) bao khong can factory");

    auto t1 = fEvm->createTransaction({"transfer", "0xa11ce", "0xb0b", 100, {}});
    auto* e1 = dynamic_cast<EvmTransaction*>(t1.get());
    CHECK(e1 && e1->nonce == 0 && e1->gasLimit == 21000 && e1->value == 100, "phieu EVM: nonce 0, gas 21000, value 100");

    threw = false;
    try { fEvm->createTransaction({"transfer", "0xa11ce", "abc", 1, {}}); } catch (const TxBuildError&) { threw = true; }
    CHECK(threw, "dia chi EVM sai mau -> TxBuildError");

    ledgerB->addUtxo({"seed:0", "carol_b", 30, false});
    ledgerB->addUtxo({"seed:1", "carol_b", 50, false});
    auto t2 = fUtxo->createTransaction({"transfer", "carol_b", "dave_bb", 60, {}});
    auto* u2 = dynamic_cast<UtxoTransaction*>(t2.get());
    CHECK(u2 && u2->inputs.size() == 2 && u2->outputs.size() == 2 && u2->outputs[1].amount == 20,
          "phieu UTXO: chon 2 to (30+50), tra 60, thoi 20");
    threw = false;
    try { fUtxo->createTransaction({"transfer", "carol_b", "dave_bb", 500, {}}); } catch (const TxBuildError&) { threw = true; }
    CHECK(threw, "UTXO khong du to -> TxBuildError");

    // ------------------------------------------------------------------
    std::cout << "B. EVMAdapter\n";
    auto adapterA = std::make_shared<EVMAdapter>(ledgerA);
    auto r = adapterA->submit(*e1);
    CHECK(r.ok() && adapterA->getBalance("0xa11ce") == 900 && adapterA->getBalance("0xb0b") == 100,
          "transfer 100: Alice 900, Bob 100");
    CHECK(ledgerA->getAccount("0xa11ce")->nonce == 1, "nonce cua Alice tang len 1");
    r = adapterA->submit(*e1);
    CHECK(r.status == TxStatus::REVERTED && contains(r.revertReason, "nonce"), "gui lai phieu cu -> REVERTED (nonce)");

    std::optional<Event> locked;
    adapterA->listenEvent("Locked", [&](const Event& e) { locked = e; });

    std::cout << "TH5. Khong du tien\n";
    r = adapterA->sendTransaction({"lock", "0xa11ce", "0xb41d6e", 5000, {{"receiver", "alice_b"}}});
    CHECK(r.status == TxStatus::REVERTED && contains(r.revertReason, "không đủ tiền"), "lock 5000 khi co 900 -> REVERTED");
    CHECK(adapterA->getBalance("0xa11ce") == 900 && !locked, "so du giu nguyen, khong co su kien Locked");

    // ------------------------------------------------------------------
    std::cout << "TH1. Thanh cong (EVM -> UTXO, adapter that)\n";
    auto adapterB = std::make_shared<UtxoAdapter>(ledgerB);
    auto validator = std::make_shared<TestProofValidator>();
    TokenBridge bridge(adapterA, adapterB, validator);

    r = adapterA->sendTransaction({"lock", "0xa11ce", "0xb41d6e", 100, {{"receiver", "alice_b"}}});
    CHECK(r.ok() && locked && locked->fields.at("amount") == "100", "lock 100 thanh cong, nghe duoc su kien Locked");
    CHECK(adapterA->getBalance("0xb41d6e") == 100 &&
          adapterA->getContractState("0xb41d6e").get("lockedTotal") == std::optional<std::string>("100"),
          "ket bridge giu 100, lockedTotal = 100");

    auto msg = messageFromLocked(*locked, "ChainA-EVM", "ChainB-UTXO");
    r = bridge.relay(msg);
    CHECK(r.ok() && msg.status == MessageStatus::EXECUTED, "relay -> SUCCESS, thu EXECUTED");
    CHECK(adapterB->getBalance("alice_b") == 100, "alice_b nhan 100 o ChainB");

    std::cout << "TH3. Gui lai thu cu\n";
    auto again = msg;
    r = bridge.relay(again);
    CHECK(r.status == TxStatus::FAILED && adapterB->getBalance("alice_b") == 100, "bi tu choi, alice_b van 100");

    std::cout << "TH2. Thu gia\n";
    auto fake = signedMessage("alice_b", 1000000, 99);
    fake.proof = "FORGED";
    r = bridge.relay(fake);
    CHECK(r.status == TxStatus::FAILED && adapterB->getBalance("alice_b") == 100, "thu gia bi chan, so du khong doi");

    std::cout << "TH4. Sai dia chi\n";
    auto badAddr = signedMessage("abc", 100, 1);
    r = bridge.relay(badAddr);
    CHECK(r.status == TxStatus::FAILED && contains(r.revertReason, "sai mẫu"), "Factory bao dia chi sai mau -> FAILED");
    CHECK(badAddr.status == MessageStatus::FAILED, "thu o trang thai FAILED");

    std::cout << "TH6. Tieu 1 to 2 lan\n";
    auto aliceUtxos = ledgerB->unspentOf("alice_b");
    CHECK(aliceUtxos.size() == 1, "alice_b co dung 1 to");
    const std::string id = aliceUtxos.at(0).id;
    UtxoTransaction pay1, pay2;
    pay1.inputs = {{id, UtxoTransactionFactory::fakeSign("alice_b", id)}};
    pay1.outputs = {{"dave_bb", 100}};
    pay1.txHash = "pay1";
    pay2 = pay1;
    pay2.outputs = {{"erin_bb", 100}};
    pay2.txHash = "pay2";
    CHECK(adapterB->submit(pay1).ok(), "lan 1: tra cho dave_bb thanh cong");
    r = adapterB->submit(pay2);
    CHECK(r.status == TxStatus::REVERTED && contains(r.revertReason, "đã được tiêu"), "lan 2: REVERTED (to da tieu)");
    CHECK(adapterB->getBalance("erin_bb") == 0, "erin_bb khong nhan duoc gi");

    std::cout << "TH7. Chain dich het gas + khoi phuc bang snapshot\n";
    auto ledgerC = std::make_shared<LocalLedgerSimulator>("ChainC-EVM");
    auto adapterC = std::make_shared<EVMAdapter>(ledgerC);
    TokenBridge bridgeToEvm(adapterA, adapterC, validator);
    auto snapC = ledgerC->createSnapshot("before-act");
    auto lowGas = signedMessage("0xa11ce", 100, 7, "ChainC-EVM");
    lowGas.payload["gasLimit"] = "1000";
    lowGas.messageHash = lowGas.computeHash();
    lowGas.proof = "SIGNED:" + lowGas.messageHash;
    r = bridgeToEvm.relay(lowGas);
    CHECK(r.status == TxStatus::REVERTED && contains(r.revertReason, "hết gas"), "mint tren ChainC het gas -> REVERTED");
    CHECK(adapterC->getBalance("0xa11ce") == 0 && ledgerC->getAccount("0x0")->nonce == 1,
          "khong ai nhan tien, nhung nonce he thong da tang (giong EVM that)");
    ledgerC->restore(snapC);
    CHECK(!ledgerC->getAccount("0x0").has_value(), "restore(): ChainC tro ve dung nhu truoc");

    // ------------------------------------------------------------------
    std::cout << "C. MockChainAdapter (gay loi co chu dich)\n";
    auto mock = std::make_shared<MockChainAdapter>("MockChain");
    mock->injectFailure(TxStatus::FAILED, "node khong phan hoi");
    TokenBridge bridgeToMock(adapterA, mock, validator);
    auto m1 = signedMessage("bob", 50, 11, "MockChain");
    r = bridgeToMock.relay(m1);
    CHECK(r.status == TxStatus::FAILED && r.revertReason == "node khong phan hoi", "loi cai san duoc tra ve");
    auto m2 = signedMessage("bob", 50, 12, "MockChain");
    r = bridgeToMock.relay(m2);
    CHECK(r.ok() && mock->getBalance("bob") == 50, "lan sau binh thuong: bob nhan 50");

    std::cout << "\n" << passes << " dat, " << failures << " khong dat\n";
    return failures == 0 ? 0 : 1;
}
