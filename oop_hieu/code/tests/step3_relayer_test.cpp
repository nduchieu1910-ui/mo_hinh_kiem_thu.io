// =====================================================================
//  Bước 3 — CrossChainRelayer + Proof Validation giả lập (MultiSig M-of-N)
// =====================================================================
#include "cc/adapter/EVMAdapter.h"
#include "cc/adapter/MockChainAdapter.h"
#include "cc/adapter/SolanaAdapter.h"
#include "cc/adapter/UtxoAdapter.h"
#include "cc/bridge/MultiSigProofValidator.h"
#include "cc/ledger/LocalLedgerSimulator.h"
#include "cc/relayer/CrossChainRelayer.h"

#include <iostream>
#include <memory>

using namespace cc;

static int failures = 0, passes = 0;
#define CHECK(cond, name)                                                         \
    do {                                                                          \
        if (cond) { std::cout << "  [PASS] " << name << "\n"; ++passes; }         \
        else { std::cout << "  [FAIL] " << name << "\n"; ++failures; }            \
    } while (0)

static bool contains(const std::string& s, const std::string& p) { return s.find(p) != std::string::npos; }
static Address sol(const std::string& n) { return n + std::string(32 - n.size(), '1'); }
static const std::vector<Address> kValidators{"val_a", "val_b", "val_c"};

static std::shared_ptr<EVMAdapter> evmWithAlice(const std::string& name, Amount balance = 1000) {
    auto l = std::make_shared<LocalLedgerSimulator>(name);
    l->upsertAccount({"0xa11ce", balance, 0, false});
    return std::make_shared<EVMAdapter>(l);
}

static TransactionResult userLock(IBlockchainAdapter& a, const Address& from, Amount amount, const Address& receiver) {
    return a.sendTransaction({"lock", from, "", amount, {{"receiver", receiver}}});
}

int main() {
    // ------------------------------------------------------------------
    std::cout << "A. Relayer tu nghe su kien va chuyen thu (EVM -> UTXO, 2/3 chu ky)\n";
    auto evmA = evmWithAlice("ChainA-EVM");
    auto utxoB = std::make_shared<UtxoAdapter>(std::make_shared<LocalLedgerSimulator>("ChainB-UTXO"));
    auto val = std::make_shared<MultiSigProofValidator>(kValidators, 2);
    auto bridge = std::make_shared<TokenBridge>(evmA, utxoB, val);
    CrossChainRelayer relayer(bridge, evmA, {3, true, "0x0e7a"});
    relayer.start();

    CHECK(userLock(*evmA, "0xa11ce", 100, "alice_b").ok(), "Alice tu khoa 100 o ChainA");
    CHECK(relayer.pendingCount() == 1 && utxoB->getBalance("alice_b") == 0, "relayer da nhan su kien, chua chuyen");
    relayer.processPending();
    const RelayRecord r0 = relayer.records().at(0);  // sao chép: vector có thể cấp phát lại
    CHECK(r0.status == RelayStatus::EXECUTED && r0.attempts == 1 && r0.message.status == MessageStatus::EXECUTED,
          "vong 1: thu EXECUTED sau 1 lan thu");
    CHECK(utxoB->getBalance("alice_b") == 100, "alice_b nhan 100 o ChainB");
    CHECK(!relayer.onLockedEvent(r0.sourceEvent) && relayer.records().size() == 1, "su kien trung bi bo qua");

    userLock(*evmA, "0xa11ce", 20, "alice_b");
    userLock(*evmA, "0xa11ce", 30, "bob_bbb");
    relayer.runUntilIdle();
    CHECK(relayer.count(RelayStatus::EXECUTED) == 3 && relayer.records().at(2).message.nonce == 2,
          "3 thu deu EXECUTED, nonce tang 0 -> 1 -> 2");
    CHECK(utxoB->getBalance("alice_b") == 120 && utxoB->getBalance("bob_bbb") == 30, "so du ChainB dung");

    // ------------------------------------------------------------------
    std::cout << "B. Proof Validation gia lap\n";
    CrossChainMessage m;
    m.sourceChain = "ChainA-EVM"; m.targetChain = "ChainB-UTXO";
    m.sender = "0xa11ce"; m.receiver = "alice_b"; m.amount = 5; m.nonce = 50;
    m.messageHash = m.computeHash();
    m.proof = val->createProof(m);
    CHECK(val->validate(m).empty(), "du 3/3 chu ky -> hop le");
    m.proof = MultiSigProofValidator::attest(m, {"val_a"});
    CHECK(contains(val->validate(m), "1/2"), "chi 1 chu ky -> khong du (1/2)");
    m.proof = MultiSigProofValidator::attest(m, {"val_a", "val_x"});
    CHECK(contains(val->validate(m), "1/2"), "nguoi ky la (val_x) khong duoc tinh");
    m.proof = MultiSigProofValidator::attest(m, {"val_a", "val_a"});
    CHECK(contains(val->validate(m), "1/2"), "mot nguoi ky 2 lan chi tinh 1");
    m.proof = MultiSigProofValidator::attest(m, {"val_a", "val_c"});
    m.amount = 999;
    CHECK(contains(val->validate(m), "bị sửa"), "sua so tien sau khi ky -> ma bam khong khop");
    CHECK(contains(val->validate(r0.message), "replay"), "thu da thuc thi -> replay");
    CrossChainMessage sameNonce = r0.message;
    sameNonce.receiver = "mallory_b";
    sameNonce.messageHash = sameNonce.computeHash();
    sameNonce.proof = val->createProof(sameNonce);
    CHECK(contains(val->validate(sameNonce), "replay"), "noi dung khac nhung trung nonce -> replay");
    CrossChainMessage wrongChain = m;
    wrongChain.amount = 5; wrongChain.targetChain = "ChainZ";
    wrongChain.messageHash = wrongChain.computeHash();
    wrongChain.proof = val->createProof(wrongChain);
    CHECK(contains(bridge->verify(wrongChain), "chain đích"), "thu gui nham chain dich -> bridge tu choi");

    // ------------------------------------------------------------------
    std::cout << "C. Thu lai khi chain dich loi tam thoi\n";
    auto evmA2 = evmWithAlice("ChainA2-EVM");
    auto mockB = std::make_shared<MockChainAdapter>("MockB");
    mockB->injectFailure(TxStatus::FAILED, "node khong phan hoi", 1);
    CrossChainRelayer relayer2(std::make_shared<TokenBridge>(evmA2, mockB, std::make_shared<MultiSigProofValidator>(kValidators, 2)),
                               evmA2, {3, true, "0x0e7a"});
    relayer2.start();
    userLock(*evmA2, "0xa11ce", 50, "bob");
    relayer2.processPending();
    CHECK(relayer2.records().at(0).status == RelayStatus::RETRYING && relayer2.records().at(0).lastError == "node khong phan hoi",
          "vong 1: loi -> RETRYING");
    relayer2.processPending();
    CHECK(relayer2.records().at(0).status == RelayStatus::EXECUTED && relayer2.records().at(0).attempts == 2 &&
          mockB->getBalance("bob") == 50, "vong 2: thanh cong, bob nhan 50");

    // ------------------------------------------------------------------
    std::cout << "D. Truong hop 7: chain dich hong han -> hoan tien\n";
    auto evmA3 = evmWithAlice("ChainA3-EVM");
    auto mockC = std::make_shared<MockChainAdapter>("MockC");
    mockC->injectFailure(TxStatus::REVERTED, "het gas", 5);
    CrossChainRelayer relayer3(std::make_shared<TokenBridge>(evmA3, mockC, std::make_shared<MultiSigProofValidator>(kValidators, 2)),
                               evmA3, {3, true, "0x0e7a"});
    relayer3.start();
    userLock(*evmA3, "0xa11ce", 70, "bob");
    CHECK(evmA3->getBalance("0xa11ce") == 930 && evmA3->getBalance("0xb41d6e") == 70, "sau khi khoa: Alice 930, ket 70");
    relayer3.runUntilIdle();
    const RelayRecord r3 = relayer3.records().at(0);
    CHECK(r3.status == RelayStatus::REFUNDED && r3.attempts == 3 && !r3.refundTxHash.empty(), "thu 3 lan deu loi -> REFUNDED");
    CHECK(evmA3->getBalance("0xa11ce") == 1000 && evmA3->getBalance("0xb41d6e") == 0, "Alice nhan lai du 1000, ket ve 0");
    CHECK(evmA3->getContractState("0xb41d6e").get("lockedTotal") == std::optional<std::string>("0"), "lockedTotal ve 0");
    CHECK(r3.log.size() >= 4, "co nhat ky tung buoc cho dashboard");

    // ------------------------------------------------------------------
    std::cout << "E. Validator ngung hoat dong -> thu bi tu choi, khong thu lai\n";
    auto evmA4 = evmWithAlice("ChainA4-EVM");
    auto utxoB4 = std::make_shared<UtxoAdapter>(std::make_shared<LocalLedgerSimulator>("ChainB4-UTXO"));
    auto val4 = std::make_shared<MultiSigProofValidator>(kValidators, 2);
    val4->setActiveSigners({"val_a"});            // 2 validator con lai bi sap
    CrossChainRelayer relayer4(std::make_shared<TokenBridge>(evmA4, utxoB4, val4), evmA4, {3, true, "0x0e7a"});
    relayer4.start();
    userLock(*evmA4, "0xa11ce", 40, "alice_b");
    relayer4.processPending();
    const RelayRecord r4 = relayer4.records().at(0);
    CHECK(r4.status == RelayStatus::REJECTED && contains(r4.lastError, "không đủ chữ ký"), "chi 1/2 chu ky -> REJECTED");
    CHECK(relayer4.pendingCount() == 0 && utxoB4->getBalance("alice_b") == 0 && evmA4->getBalance("0xb41d6e") == 40,
          "khong thu lai, khong mint; tien van trong ket cho nguoi kiem tra");

    // ------------------------------------------------------------------
    std::cout << "F. Doi chieu: Solana -> EVM, relayer giu nguyen\n";
    auto lS = std::make_shared<LocalLedgerSimulator>("ChainS-Solana");
    const Address aliceS = sol("A1iceSo1ana");
    lS->upsertAccount({aliceS, 500, 0, false});
    auto solA = std::make_shared<SolanaAdapter>(lS);
    auto evmE = std::make_shared<EVMAdapter>(std::make_shared<LocalLedgerSimulator>("ChainE-EVM"));
    CrossChainRelayer relayer5(std::make_shared<TokenBridge>(solA, evmE, std::make_shared<MultiSigProofValidator>(kValidators, 2)),
                               solA, {3, true, sol("Refunder1")});
    relayer5.start();
    userLock(*solA, aliceS, 60, "0xca201");
    relayer5.processPending();
    CHECK(relayer5.records().at(0).status == RelayStatus::EXECUTED && evmE->getBalance("0xca201") == 60,
          "Solana khoa 60 -> EVM nhan 60");

    std::cout << "\n" << passes << " dat, " << failures << " khong dat\n";
    return failures == 0 ? 0 : 1;
}
