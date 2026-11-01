#include "cc/testing/Scenarios.h"

#include "cc/core/HashUtil.h"

namespace cc::scenarios {

Address solAddr(const std::string& name) { return name + std::string(32 - name.size(), '1'); }
const Address& kAliceSol() { static const Address a = solAddr("A1iceSo1ana"); return a; }
const Address& kSolVault() { static const Address a = "BridgeVau1t11111111111111111111111"; return a; }

void buildStandardWorld(Sandbox& sb) {
    sb.addEvmChain(kChainA);
    sb.addUtxoChain(kChainB);
    sb.addMockChain(kChainM);
    sb.addSolanaChain(kChainS);
    sb.addEvmChain(kLendA);
    sb.addSolanaChain(kLendS);

    sb.connect(kChainA, kChainB);
    sb.connect(kChainA, kChainM);
    sb.connect(kChainA, kChainS);
    RouteOptions back;
    back.kind = BridgeKind::Unlock;
    sb.connect(kChainS, kChainA, back);

    RouteOptions lend;
    lend.kind = BridgeKind::Lending;
    sb.connect(kLendA, kLendS, lend);
    RouteOptions release;
    release.kind = BridgeKind::Unlock;
    release.eventName = "CollateralReleased";
    release.refundOnFailure = false;
    sb.connect(kLendS, kLendA, release);
}

namespace {

bool contains(const std::string& s, const std::string& p) { return s.find(p) != std::string::npos; }

// TH1 — Alice khóa 100 ở A, relayer chuyển, alice_b nhận 100 ở B.
class LockAndMintTest : public TestCase {
public:
    LockAndMintTest() : TestCase("TH1_thanh_cong", "Alice khóa 100 ở ChainA, alice_b nhận 100 ở ChainB") {}
protected:
    void arrange(Sandbox& sb) override { sb.setBalance(kChainA, kAlice, 1000); }
    void act(Sandbox& sb) override {
        lock_ = sb.lock(kChainA, kChainB, kAlice, 100, "alice_b");
        sb.relayAll();
    }
    void assertResult(Sandbox& sb) override {
        sb.expectTxStatus(lock_, TxStatus::SUCCESS, "giao dịch khóa ở ChainA");
        sb.expectBalance(kChainA, kAlice, 900);
        sb.expectBalance(kChainB, "alice_b", 100);
        sb.expectRelayStatus(kChainA, kChainB, 0, RelayStatus::EXECUTED);
        sb.expectStorage(kChainA, kEscrow, "lockedTotal", "100");
    }
private:
    TransactionResult lock_;
};

// TH2 — thư có con dấu giả bị bridge từ chối.
class ForgedMessageTest : public TestCase {
public:
    ForgedMessageTest() : TestCase("TH2_thu_gia", "Thư 'phát 1 triệu' với con dấu giả bị chặn") {}
protected:
    void arrange(Sandbox& sb) override { sb.setBalance(kChainA, kAlice, 1000); }
    void act(Sandbox& sb) override {
        CrossChainMessage m = sb.makeMessage(kChainA, kChainB, kAlice, "alice_b", 1000000, 99);
        m.proof = MultiSigProofValidator::attest(m, {"hacker_1", "hacker_2"});
        res_ = sb.injectMessage(kChainA, kChainB, m);
    }
    void assertResult(Sandbox& sb) override {
        sb.expectTxStatus(res_, TxStatus::FAILED, "relay thư giả");
        sb.expectTrue(contains(res_.revertReason, "không đủ chữ ký"), "lý do: không đủ chữ ký hợp lệ");
        sb.expectBalance(kChainB, "alice_b", 0);
    }
private:
    TransactionResult res_;
};

// TH3 — gửi lại thư đã thực thi.
class ReplayTest : public TestCase {
public:
    ReplayTest() : TestCase("TH3_gui_lai_thu_cu", "Gửi lại thư nonce 0 đã thực thi bị chặn (replay)") {}
protected:
    void arrange(Sandbox& sb) override {          // dựng sẵn tình huống: thư 0 đã chạy xong
        sb.setBalance(kChainA, kAlice, 1000);
        sb.lock(kChainA, kChainB, kAlice, 100, "alice_b");
        sb.relayAll();
    }
    void act(Sandbox& sb) override {
        CrossChainMessage old = sb.relayRecords(kChainA, kChainB).at(0).message;
        res_ = sb.injectMessage(kChainA, kChainB, old);
    }
    void assertResult(Sandbox& sb) override {
        sb.expectTxStatus(res_, TxStatus::FAILED, "relay lại thư cũ");
        sb.expectTrue(contains(res_.revertReason, "replay"), "lý do: replay");
        sb.expectBalance(kChainB, "alice_b", 100);
    }
private:
    TransactionResult res_;
};

// TH4 — người nhận sai mẫu: chain đích từ chối, relayer thử 3 lần rồi hoàn tiền.
class BadAddressTest : public TestCase {
public:
    BadAddressTest() : TestCase("TH4_sai_dia_chi", "Người nhận 'abc' sai mẫu: thử 3 lần rồi hoàn tiền cho Alice") {}
protected:
    void arrange(Sandbox& sb) override { sb.setBalance(kChainA, kAlice, 1000); }
    void act(Sandbox& sb) override {
        sb.lock(kChainA, kChainB, kAlice, 100, "abc");
        sb.relayAll();
    }
    void assertResult(Sandbox& sb) override {
        sb.expectRelayStatus(kChainA, kChainB, 0, RelayStatus::REFUNDED);
        sb.expectTrue(contains(sb.relayRecords(kChainA, kChainB).at(0).lastError, "sai mẫu"), "lý do: địa chỉ sai mẫu");
        sb.expectBalance(kChainA, kAlice, 1000);
        sb.expectBalance(kChainA, kEscrow, 0);
    }
};

// TH5 — không đủ tiền: khóa bị REVERTED, không có thư nào.
class InsufficientFundsTest : public TestCase {
public:
    InsufficientFundsTest() : TestCase("TH5_khong_du_tien", "Alice có 50, khóa 100 bị REVERTED, không sinh thư") {}
protected:
    void arrange(Sandbox& sb) override { sb.setBalance(kChainA, kAlice, 50); }
    void act(Sandbox& sb) override {
        lock_ = sb.lock(kChainA, kChainB, kAlice, 100, "alice_b");
        sb.relayAll();
    }
    void assertResult(Sandbox& sb) override {
        sb.expectTxStatus(lock_, TxStatus::REVERTED, "giao dịch khóa ở ChainA");
        sb.expectRelayCount(kChainA, kChainB, 0);
        sb.expectBalance(kChainA, kAlice, 50);
    }
private:
    TransactionResult lock_;
};

// TH6 — tiêu một tờ tiền hai lần ở ChainB.
class DoubleSpendTest : public TestCase {
public:
    DoubleSpendTest() : TestCase("TH6_tieu_1_to_2_lan", "alice_b dùng cùng tờ 100 trả 2 người: lần 2 bị chặn") {}
protected:
    void arrange(Sandbox& sb) override { utxo_ = sb.giveUtxo(kChainB, "alice_b", 100); }
    void act(Sandbox& sb) override {
        UtxoTransaction p1, p2;
        p1.inputs = {{utxo_, simSign("alice_b", utxo_)}};
        p1.outputs = {{"dave_bb", 100}};
        p1.txHash = "pay_dave";
        p2 = p1;
        p2.outputs = {{"erin_bb", 100}};
        p2.txHash = "pay_erin";
        r1_ = sb.submitRaw(kChainB, p1);
        r2_ = sb.submitRaw(kChainB, p2);
    }
    void assertResult(Sandbox& sb) override {
        sb.expectTxStatus(r1_, TxStatus::SUCCESS, "lần trả 1 (cho dave_bb)");
        sb.expectTxStatus(r2_, TxStatus::REVERTED, "lần trả 2 (cho erin_bb)");
        sb.expectBalance(kChainB, "dave_bb", 100);
        sb.expectBalance(kChainB, "erin_bb", 0);
    }
private:
    std::string utxo_;
    TransactionResult r1_, r2_;
};

// TH7 — chain đích hỏng hẳn: relayer thử 3 lần rồi hoàn tiền.
class TargetDownTest : public TestCase {
public:
    TargetDownTest() : TestCase("TH7_chain_dich_hong", "ChainM hết gas 5 lần liên tiếp: thử 3 lần, hoàn 70 cho Alice") {}
protected:
    void arrange(Sandbox& sb) override {
        sb.setBalance(kChainA, kAlice, 1000);
        sb.failNext(kChainM, TxStatus::REVERTED, "hết gas", 5);       // MOCKING
    }
    void act(Sandbox& sb) override {
        sb.lock(kChainA, kChainM, kAlice, 70, "bob");
        sb.relayAll();
    }
    void assertResult(Sandbox& sb) override {
        sb.expectRelayStatus(kChainA, kChainM, 0, RelayStatus::REFUNDED);
        sb.expectTrue(sb.relayRecords(kChainA, kChainM).at(0).attempts == 3, "đã thử đúng 3 lần");
        sb.expectBalance(kChainA, kAlice, 1000);
        sb.expectBalance(kChainA, kEscrow, 0);
        sb.expectBalance(kChainM, "bob", 0);
    }
};

// ===================== Ứng dụng Token Bridge thực tế =====================

// TH9 — chuyển khứ hồi: A -> Solana rồi Solana -> A, Alice nhận lại đúng số tiền.
class RoundTripTest : public TestCase {
public:
    RoundTripTest() : TestCase("TH9_chuyen_khu_hoi", "Alice chuyển 100 từ EVM sang Solana rồi chuyển về: nhận lại đủ") {}
protected:
    void arrange(Sandbox& sb) override { sb.setBalance(kChainA, kAlice, 1000); }
    void act(Sandbox& sb) override {
        sb.lock(kChainA, kChainS, kAlice, 100, kAliceSol());
        sb.relayAll();
        sb.lock(kChainS, kChainA, kAliceSol(), 100, kAlice);
        sb.relayAll();
    }
    void assertResult(Sandbox& sb) override {
        sb.expectRelayStatus(kChainA, kChainS, 0, RelayStatus::EXECUTED);
        sb.expectRelayStatus(kChainS, kChainA, 0, RelayStatus::EXECUTED);
        sb.expectBalance(kChainA, kAlice, 1000);
        sb.expectBalance(kChainA, kEscrow, 0);
        sb.expectBalance(kChainS, kAliceSol(), 0);
    }
};

// TH10 — nhiều người cùng chuyển: relayer xử lý đúng thứ tự, không lẫn tiền.
class MultiUserTest : public TestCase {
public:
    MultiUserTest() : TestCase("TH10_nhieu_nguoi", "Alice, Bob, Carol cùng chuyển sang ChainB: đúng thứ tự nonce, đúng người nhận") {}
protected:
    void arrange(Sandbox& sb) override {
        sb.setBalance(kChainA, kAlice, 1000);
        sb.setBalance(kChainA, "0xb0b", 500);
        sb.setBalance(kChainA, "0xca201", 300);
    }
    void act(Sandbox& sb) override {
        sb.lock(kChainA, kChainB, kAlice, 100, "alice_b");
        sb.lock(kChainA, kChainB, "0xb0b", 200, "bob_bbb");
        sb.lock(kChainA, kChainB, "0xca201", 300, "carol_b");
        sb.relayAll();
    }
    void assertResult(Sandbox& sb) override {
        sb.expectRelayCount(kChainA, kChainB, 3);
        const auto& recs = sb.relayRecords(kChainA, kChainB);
        bool ordered = recs.size() == 3;
        for (std::size_t i = 0; ordered && i < 3; ++i)
            ordered = recs[i].message.nonce == i && recs[i].status == RelayStatus::EXECUTED;
        sb.expectTrue(ordered, "3 thư EXECUTED theo đúng nonce 0, 1, 2");
        sb.expectBalance(kChainB, "alice_b", 100);
        sb.expectBalance(kChainB, "bob_bbb", 200);
        sb.expectBalance(kChainB, "carol_b", 300);
        sb.expectBalance(kChainA, kEscrow, 600);
    }
};

// ===================== Ứng dụng Cho vay xuyên chuỗi (Lending) =====================
//  Alice thế chấp ở LendA-EVM, vay ở LendS-Solana. Quỹ cho vay = két bridge trên Solana.
//  Oracle giá (mock): priceX100 = 100 (1:1), LTV 50%, ngưỡng thanh lý 80%.

const char* kLiquidatorHome = "0x11d";
Address liquidatorSol() { return solAddr("Liquidatr1"); }

class LendingTest : public TestCase {
public:
    LendingTest(std::string n, std::string d) : TestCase(std::move(n), std::move(d), "Lending") {}
protected:
    static void setupPool(Sandbox& sb) {
        sb.setBalance(kLendA, kAlice, 5000);
        sb.setBalance(kLendS, kSolVault(), 100000);                 // quỹ cho vay
        sb.setStorage(kLendS, kSolVault(), "priceX100", "100");     // oracle giá (mock)
        sb.setStorage(kLendS, kSolVault(), "ltvBps", "5000");
        sb.setStorage(kLendS, kSolVault(), "liqBps", "8000");
    }
    static void borrow(Sandbox& sb, Amount collateral, Amount loan) {
        sb.lock(kLendA, kLendS, kAlice, collateral, kAliceSol(), "borrow=" + amountToString(loan));
        sb.relayAll();
    }
    static std::string debtKey() { return "debt[" + kAliceSol() + "]"; }
    static std::string collKey() { return "collateral[" + kAliceSol() + "]"; }
};

class BorrowWithinLimit : public LendingTest {
public:
    BorrowWithinLimit() : LendingTest("L1_vay_trong_han_muc", "Thế chấp 1000, vay 400 (tối đa 500): được vay") {}
protected:
    void arrange(Sandbox& sb) override { setupPool(sb); }
    void act(Sandbox& sb) override { borrow(sb, 1000, 400); }
    void assertResult(Sandbox& sb) override {
        sb.expectRelayStatus(kLendA, kLendS, 0, RelayStatus::EXECUTED);
        sb.expectBalance(kLendS, kAliceSol(), 400);
        sb.expectStorage(kLendS, kSolVault(), debtKey(), "400");
        sb.expectStorage(kLendS, kSolVault(), collKey(), "1000");
        sb.expectBalance(kLendA, kEscrow, 1000);
    }
};

class BorrowOverLimit : public LendingTest {
public:
    BorrowOverLimit() : LendingTest("L2_vay_vuot_han_muc", "Thế chấp 1000, đòi vay 700: bị từ chối, thế chấp được hoàn") {}
protected:
    void arrange(Sandbox& sb) override { setupPool(sb); }
    void act(Sandbox& sb) override { borrow(sb, 1000, 700); }
    void assertResult(Sandbox& sb) override {
        sb.expectRelayStatus(kLendA, kLendS, 0, RelayStatus::REFUNDED);
        sb.expectTrue(sb.relayRecords(kLendA, kLendS).at(0).lastError.find("vượt hạn mức") != std::string::npos,
                      "lý do: vượt hạn mức vay");
        sb.expectBalance(kLendS, kAliceSol(), 0);
        sb.expectBalance(kLendA, kAlice, 5000);
        sb.expectBalance(kLendA, kEscrow, 0);
    }
};

class RepayAndRelease : public LendingTest {
public:
    RepayAndRelease() : LendingTest("L3_tra_no_nhan_lai_the_chap", "Trả đủ 400: thế chấp 1000 tự động trả về Alice ở EVM") {}
protected:
    void arrange(Sandbox& sb) override { setupPool(sb); borrow(sb, 1000, 400); }
    void act(Sandbox& sb) override {
        repay_ = sb.send(kLendS, {"repay", kAliceSol(), "", 400, {}});
        sb.relayAll();
    }
    void assertResult(Sandbox& sb) override {
        sb.expectTxStatus(repay_, TxStatus::SUCCESS, "giao dịch trả nợ trên Solana");
        sb.expectStorage(kLendS, kSolVault(), debtKey(), "0");
        sb.expectRelayStatus(kLendS, kLendA, 0, RelayStatus::EXECUTED);
        sb.expectBalance(kLendA, kAlice, 5000);
        sb.expectBalance(kLendA, kEscrow, 0);
    }
private:
    TransactionResult repay_;
};

class PartialRepay : public LendingTest {
public:
    PartialRepay() : LendingTest("L4_tra_mot_phan", "Trả 200/400: chưa hết nợ nên thế chấp vẫn bị giữ") {}
protected:
    void arrange(Sandbox& sb) override { setupPool(sb); borrow(sb, 1000, 400); }
    void act(Sandbox& sb) override {
        sb.send(kLendS, {"repay", kAliceSol(), "", 200, {}});
        sb.relayAll();
    }
    void assertResult(Sandbox& sb) override {
        sb.expectStorage(kLendS, kSolVault(), debtKey(), "200");
        sb.expectRelayCount(kLendS, kLendA, 0);
        sb.expectBalance(kLendA, kAlice, 4000);
        sb.expectBalance(kLendA, kEscrow, 1000);
    }
};

class LiquidateOnPriceDrop : public LendingTest {
public:
    LiquidateOnPriceDrop() : LendingTest("L5_thanh_ly_khi_gia_giam", "Giá thế chấp giảm 60%: người thanh lý trả nợ, nhận thế chấp ở EVM") {}
protected:
    void arrange(Sandbox& sb) override {
        setupPool(sb);
        borrow(sb, 1000, 400);
        sb.setBalance(kLendS, liquidatorSol(), 2000);
    }
    void act(Sandbox& sb) override {
        sb.setStorage(kLendS, kSolVault(), "priceX100", "40");      // MOCK oracle: giá giảm 60%
        liq_ = sb.send(kLendS, {"liquidate", liquidatorSol(), kAliceSol(), 400, {{"receiverOnHome", kLiquidatorHome}}});
        sb.relayAll();
    }
    void assertResult(Sandbox& sb) override {
        sb.expectTxStatus(liq_, TxStatus::SUCCESS, "giao dịch thanh lý trên Solana");
        sb.expectStorage(kLendS, kSolVault(), debtKey(), "0");
        sb.expectStorage(kLendS, kSolVault(), collKey(), "0");
        sb.expectRelayStatus(kLendS, kLendA, 0, RelayStatus::EXECUTED);
        sb.expectBalance(kLendA, kLiquidatorHome, 1000);
        sb.expectBalance(kLendA, kEscrow, 0);
    }
private:
    TransactionResult liq_;
};

class ReplayRelease : public LendingTest {
public:
    ReplayRelease() : LendingTest("L6_gui_lai_thu_tra_the_chap", "Gửi lại thư trả thế chấp để rút 2 lần: bị chặn replay") {}
protected:
    void arrange(Sandbox& sb) override {
        setupPool(sb);
        borrow(sb, 1000, 400);
        sb.send(kLendS, {"repay", kAliceSol(), "", 400, {}});
        sb.relayAll();
    }
    void act(Sandbox& sb) override {
        res_ = sb.injectMessage(kLendS, kLendA, sb.relayRecords(kLendS, kLendA).at(0).message);
    }
    void assertResult(Sandbox& sb) override {
        sb.expectTxStatus(res_, TxStatus::FAILED, "relay lại thư trả thế chấp");
        sb.expectTrue(res_.revertReason.find("replay") != std::string::npos, "lý do: replay");
        sb.expectBalance(kLendA, kAlice, 5000);
    }
private:
    TransactionResult res_;
};

class LiquidateHealthyLoan : public LendingTest {
public:
    LiquidateHealthyLoan() : LendingTest("L7_thanh_ly_khi_con_an_toan", "Giá chưa giảm mà đòi thanh lý: bị từ chối, nợ giữ nguyên") {}
protected:
    void arrange(Sandbox& sb) override {
        setupPool(sb);
        borrow(sb, 1000, 400);
        sb.setBalance(kLendS, liquidatorSol(), 2000);
    }
    void act(Sandbox& sb) override {
        liq_ = sb.send(kLendS, {"liquidate", liquidatorSol(), kAliceSol(), 400, {{"receiverOnHome", kLiquidatorHome}}});
    }
    void assertResult(Sandbox& sb) override {
        sb.expectTxStatus(liq_, TxStatus::REVERTED, "giao dịch thanh lý trên Solana");
        sb.expectStorage(kLendS, kSolVault(), debtKey(), "400");
        sb.expectBalance(kLendS, liquidatorSol(), 2000);
    }
private:
    TransactionResult liq_;
};

} // namespace

std::vector<std::unique_ptr<TestCase>> standardScenarios() {
    std::vector<std::unique_ptr<TestCase>> v;
    v.push_back(std::make_unique<LockAndMintTest>());
    v.push_back(std::make_unique<ForgedMessageTest>());
    v.push_back(std::make_unique<ReplayTest>());
    v.push_back(std::make_unique<BadAddressTest>());
    v.push_back(std::make_unique<InsufficientFundsTest>());
    v.push_back(std::make_unique<DoubleSpendTest>());
    v.push_back(std::make_unique<TargetDownTest>());
    return v;
}

std::vector<std::unique_ptr<TestCase>> tokenAppScenarios() {
    std::vector<std::unique_ptr<TestCase>> v;
    v.push_back(std::make_unique<RoundTripTest>());
    v.push_back(std::make_unique<MultiUserTest>());
    return v;
}

std::vector<std::unique_ptr<TestCase>> lendingScenarios() {
    std::vector<std::unique_ptr<TestCase>> v;
    v.push_back(std::make_unique<BorrowWithinLimit>());
    v.push_back(std::make_unique<BorrowOverLimit>());
    v.push_back(std::make_unique<RepayAndRelease>());
    v.push_back(std::make_unique<PartialRepay>());
    v.push_back(std::make_unique<LiquidateOnPriceDrop>());
    v.push_back(std::make_unique<ReplayRelease>());
    v.push_back(std::make_unique<LiquidateHealthyLoan>());
    return v;
}

std::vector<std::unique_ptr<TestCase>> allScenarios(bool includeFailing) {
    auto v = standardScenarios();
    for (auto& t : tokenAppScenarios()) v.push_back(std::move(t));
    for (auto& t : lendingScenarios()) v.push_back(std::move(t));
    if (includeFailing) v.push_back(intentionallyFailingScenario());
    return v;
}

std::unique_ptr<TestCase> intentionallyFailingScenario() {
    return std::make_unique<LambdaTestCase>(
        "TH8_co_y_sai", "Mong đợi sai có chủ đích (alice_b có 999) để chứng minh RevertState",
        [](Sandbox& sb) { sb.setBalance(kChainA, kAlice, 1000); },
        [](Sandbox& sb) { sb.lock(kChainA, kChainB, kAlice, 100, "alice_b"); sb.relayAll(); },
        [](Sandbox& sb) { sb.expectBalance(kChainB, "alice_b", 999); });
}

} // namespace cc::scenarios
