// =====================================================================
//  Kiểm tra code khớp mục 2.1 – 2.3 của đề tài:
//   2.1 IBlockchainAdapter + EVMAdapter / SolanaAdapter / MockChainAdapter
//       + CrossChainBridge::lockAndRelay (Lock ở A -> Mint ở B)
//   2.2 TransactionFactory::createTransaction(ChainType, Payload)
//   2.3 LocalLedgerSimulator + ExecutionEngine (chữ ký, gas, Rollback)
// =====================================================================
#include "cc/adapter/EVMAdapter.h"
#include "cc/adapter/MockChainAdapter.h"
#include "cc/adapter/SolanaAdapter.h"
#include "cc/adapter/UtxoAdapter.h"
#include "cc/bridge/HashProofValidator.h"
#include "cc/engine/ExecutionEngine.h"
#include "cc/factory/SolanaTransactionFactory.h"
#include "cc/ledger/LocalLedgerSimulator.h"

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
static Address sol(const std::string& name) { return name + std::string(32 - name.size(), '1'); }

int main() {
    // ------------------------------------------------------------------
    std::cout << "2.2 TransactionFactory::createTransaction(ChainType, Payload)\n";
    auto lEvm = std::make_shared<LocalLedgerSimulator>("ChainA-EVM");
    auto lUtxo = std::make_shared<LocalLedgerSimulator>("ChainB-UTXO");
    auto lSol = std::make_shared<LocalLedgerSimulator>("ChainS-Solana");
    lEvm->upsertAccount({"0xa11ce", 1000, 0, false});
    lUtxo->addUtxo({"seed:0", "frank_b", 50, false});
    const Address aliceS = sol("A1iceSo1ana"), bobS = sol("BobSo1ana");
    lSol->upsertAccount({aliceS, 1000, 0, false});

    auto txE = TransactionFactory::createTransaction(ChainType::EVM, {"transfer", "0xa11ce", "0xb0b", 10, {}}, lEvm);
    auto* evm = dynamic_cast<EvmTransaction*>(txE.get());
    CHECK(evm && evm->nonce == 0 && evm->gasPrice == 0 && evm->gasLimit == 21000 && evm->to == "0xb0b" &&
          evm->value == 10 && !evm->data.empty(), "EVM: du nonce, gasPrice, gasLimit, to, value, data");

    auto txU = TransactionFactory::createTransaction(ChainType::UTXO, {"transfer", "frank_b", "gina_bb", 20, {}}, lUtxo);
    auto* utxo = dynamic_cast<UtxoTransaction*>(txU.get());
    CHECK(utxo && utxo->inputs.size() == 1 && utxo->outputs.size() == 2 && utxo->signatures().size() == 1,
          "UTXO: du inputs, outputs, signatures");

    auto txS = TransactionFactory::createTransaction(ChainType::SOLANA, {"transfer", aliceS, bobS, 5, {}}, lSol);
    auto* ix = dynamic_cast<SolanaInstruction*>(txS.get());
    CHECK(ix && ix->programId == "system" && ix->recentBlockhash == "bh:0" && ix->signatures.size() == 1,
          "Solana: programId, recentBlockhash, signatures");

    // ------------------------------------------------------------------
    std::cout << "2.3 ExecutionEngine: chu ky, gas, Rollback\n";
    ExecutionEngine engine(lEvm, "0xb41d6e");
    EvmTransaction forged = *evm;
    forged.signature = "sig:0xbad:" + forged.txHash;
    auto r = engine.execute(forged);
    CHECK(r.status == TxStatus::REVERTED && contains(r.revertReason, "chữ ký"), "chu ky gia -> REVERTED");
    r = engine.execute(*evm);
    CHECK(r.ok() && r.gasUsed == 21000 && lEvm->balanceOf("0xb0b") == 10, "chu ky that -> SUCCESS, gas 21000");

    auto lock = TransactionFactory::createTransaction(ChainType::EVM, {"lock", "0xa11ce", "", 5000, {{"receiver", "x"}}}, lEvm);
    r = engine.execute(*lock);
    CHECK(r.status == TxStatus::REVERTED && lEvm->getAccount("0xa11ce")->nonce == 1,
          "lock thieu tien -> Rollback: nonce van la 1, khong bi tang");

    ExecutionEngine utxoEngine(lUtxo, "utxo_bridge");
    lUtxo->addUtxo({"dup:0", "someone", 1, false});
    UtxoTransaction clash;
    clash.inputs = {{"seed:0", simSign("frank_b", "seed:0")}};
    clash.outputs = {{"gina_bb", 50}};
    clash.txHash = "dup";                       // to ra se trung id "dup:0" -> loi giua chung
    r = utxoEngine.execute(clash);
    CHECK(r.status == TxStatus::REVERTED && contains(r.revertReason, "rollback"), "loi khi ghi so -> REVERTED");
    CHECK(!lUtxo->getUtxo("seed:0")->spent, "Rollback: to seed:0 duoc tra lai, chua tieu");

    // ------------------------------------------------------------------
    std::cout << "2.1 SolanaAdapter\n";
    auto solA = std::make_shared<SolanaAdapter>(lSol);
    r = solA->submit(*ix);
    CHECK(r.ok() && solA->getBalance(bobS) == 5 && solA->getBalance(aliceS) == 995, "transfer 5 lamports");
    r = solA->submit(*ix);
    CHECK(r.status == TxStatus::REVERTED && contains(r.revertReason, "đã được xử lý"), "gui trung giao dich -> REVERTED");
    r = solA->sendTransaction({"transfer", aliceS, "abc", 5, {}});
    CHECK(r.status == TxStatus::FAILED && contains(r.revertReason, "sai mẫu"), "dia chi khong phai base58 -> FAILED");
    auto stale = TransactionFactory::createTransaction(ChainType::SOLANA, {"transfer", aliceS, bobS, 1, {}}, lSol);
    for (int i = 0; i < 151; ++i) lSol->mineBlock();
    r = solA->submit(*stale);
    CHECK(r.status == TxStatus::REVERTED && contains(r.revertReason, "hết hạn"), "recentBlockhash qua 150 khoi -> het han");
    r = solA->sendTransaction({"lock", aliceS, "", 10, {{"receiver", "0xa11ce"}, {"computeUnitLimit", "100"}}});
    CHECK(r.status == TxStatus::REVERTED && contains(r.revertReason, "compute unit"), "vuot compute unit -> REVERTED");

    // ------------------------------------------------------------------
    std::cout << "2.1 CrossChainBridge::lockAndRelay (Lock o A -> Mint o B)\n";
    auto evmA = std::make_shared<EVMAdapter>(lEvm);
    auto proof = std::make_shared<HashProofValidator>();
    TokenBridge evmToSol(evmA, solA, proof);
    auto br = evmToSol.lockAndRelay("0xa11ce", bobS, 100);
    CHECK(br.ok() && br.message.status == MessageStatus::EXECUTED, "EVM -> Solana: lock + mint thanh cong");
    CHECK(evmA->getBalance("0xb41d6e") == 100 && solA->getBalance(bobS) == 105, "ket EVM giu 100, bob tren Solana +100");

    br = evmToSol.lockAndRelay("0xa11ce", bobS, 99999);
    CHECK(!br.lock.ok() && br.target.status == TxStatus::FAILED && contains(br.target.revertReason, "khóa"),
          "lock thieu tien -> khong mint, bao loi o buoc khoa");

    TokenBridge solToEvm(solA, evmA, proof);
    br = solToEvm.lockAndRelay(aliceS, "0xca201", 50);
    CHECK(br.ok() && evmA->getBalance("0xca201") == 50, "chieu nguoc Solana -> EVM: chi doi adapter, bridge giu nguyen");

    auto tampered = br.message;
    tampered.amount = 5000;
    CHECK(solToEvm.relay(tampered).status == TxStatus::FAILED, "sua so tien trong thu -> HashProofValidator chan");

    auto mock = std::make_shared<MockChainAdapter>("MockChain");
    TokenBridge evmToMock(evmA, mock, proof);
    br = evmToMock.lockAndRelay("0xa11ce", "bob", 30);
    CHECK(br.ok() && mock->getBalance("bob") == 30, "EVM -> MockChain: lock + mint thanh cong");

    std::cout << "\n" << passes << " dat, " << failures << " khong dat\n";
    return failures == 0 ? 0 : 1;
}
