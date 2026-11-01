// =====================================================================
//  Bước 4 — Sandbox (Arrange/Act/Assert), Snapshot & RevertState, Mocking
// =====================================================================
#include "cc/testing/Scenarios.h"

#include <iostream>

using namespace cc;
using namespace cc::scenarios;

static int failures = 0, passes = 0;
#define CHECK(cond, name)                                                         \
    do {                                                                          \
        if (cond) { std::cout << "  [PASS] " << name << "\n"; ++passes; }         \
        else { std::cout << "  [FAIL] " << name << "\n"; ++failures; }            \
    } while (0)

static bool hasChange(const TestReport& r, const std::string& key, const std::string& before, const std::string& after) {
    for (const auto& c : r.changes)
        if (c.key == key && c.before == before && c.after == after) return true;
    return false;
}

int main() {
    // ------------------------------------------------------------------
    std::cout << "A. Snapshot & RevertState khoi phuc MOI thanh phan\n";
    Sandbox sb;
    buildStandardWorld(sb);
    const SandboxSnapshot s0 = sb.snapshot("trang");

    sb.setBalance(kChainA, kAlice, 1000);
    sb.lock(kChainA, kChainB, kAlice, 100, "alice_b");
    sb.relayAll();
    sb.failNext(kChainM, TxStatus::FAILED, "loi gia", 2);
    CHECK(sb.balance(kChainB, "alice_b") == 100 && sb.relayRecords(kChainA, kChainB).size() == 1, "truoc revert: alice_b 100, 1 thu");
    CHECK(sb.relayRecords(kChainA, kChainM).empty(), "su kien gui toi ChainB khong bi relayer ChainM nhan nham");

    sb.revertState(s0);
    CHECK(sb.balance(kChainA, kAlice) == 0 && sb.balance(kChainB, "alice_b") == 0, "sau revert: so du ve 0");
    CHECK(sb.relayRecords(kChainA, kChainB).empty(), "sau revert: relayer khong con ban ghi");
    CHECK(sb.send(kChainM, {"mint", "", "bob", 5, {}}).ok(), "sau revert: loi gia da cai o Mock bien mat");
    CHECK(sb.stateView().size() == 1, "sau revert: chi con so du vua mint o Mock");
    sb.revertState(s0);
    CHECK(sb.stateView().empty(), "revert lan nua: the gioi trang tinh");

    sb.setBalance(kChainA, kAlice, 1000);
    sb.lock(kChainA, kChainB, kAlice, 100, "alice_b");
    sb.relayAll();
    CHECK(sb.relayRecords(kChainA, kChainB).at(0).status == RelayStatus::EXECUTED,
          "chay lai dung kich ban: van EXECUTED (validator da quen nonce 0)");
    sb.revertState(s0);

    std::cout << "B. Vi sao khong the chi khoi phuc so cai\n";
    const SandboxSnapshot s1 = sb.snapshot("s1");
    sb.setBalance(kChainA, kAlice, 1000);
    const SandboxSnapshot afterArrange = sb.snapshot("afterArrange");
    sb.lock(kChainA, kChainB, kAlice, 100, "alice_b");
    sb.relayAll();
    sb.ledger(kChainA)->restore(afterArrange.ledgers.at(kChainA));   // CHỈ trả sổ cái
    sb.ledger(kChainB)->restore(afterArrange.ledgers.at(kChainB));
    sb.lock(kChainA, kChainB, kAlice, 100, "alice_b");
    sb.relayAll();
    CHECK(sb.balance(kChainB, "alice_b") == 0, "chi tra so cai -> relayer/validator van nho thu cu, lan 2 KHONG chuyen duoc");
    sb.revertState(s1);
    CHECK(sb.stateView().empty(), "revertState day du -> sach se tro lai");

    // ------------------------------------------------------------------
    std::cout << "C. Chay 7 kich ban + 1 kich ban co y sai (luon revert)\n";
    TestSuite suite(sb, /*alwaysRevert=*/true);
    for (auto& tc : standardScenarios()) suite.add(std::move(tc));
    suite.add(intentionallyFailingScenario());
    auto reports = suite.runAll();
    TestSuite::printReport(std::cout, reports);
    bool first7 = true;
    for (int i = 0; i < 7; ++i) first7 = first7 && reports.at(i).passed;
    CHECK(reports.size() == 8 && first7, "7 kich ban chuan deu DAT");
    CHECK(!reports.at(7).passed && reports.at(7).reverted, "kich ban co y sai: KHONG DAT va da revertState");
    bool allReverted = true;
    for (const auto& r : reports) allReverted = allReverted && r.reverted;
    CHECK(allReverted && sb.stateView().empty(), "sau ca bo test: moi thu ve trang thai ban dau");
    CHECK(hasChange(reports.at(0), "ChainA-EVM/account/0xa11ce/balance", "1000", "900"),
          "bao cao TH1 ghi thay doi: so du Alice 1000 -> 900");
    CHECK(hasChange(reports.at(0), "ChainA-EVM->ChainB-UTXO/relay/0", "", "EXECUTED sau 1 lần"),
          "bao cao TH1 ghi thay doi: thu #0 EXECUTED");

    std::cout << "D. Cac test doc lap voi nhau\n";
    auto again = standardScenarios();
    CHECK(again.at(0)->run(sb).passed && again.at(0)->run(sb).passed, "chay TH1 hai lan lien tiep: ca hai DAT");

    std::cout << "E. Che do chi revert khi KHONG DAT (dung cau chu de tai)\n";
    Sandbox sb2;
    buildStandardWorld(sb2);
    TestSuite suite2(sb2, /*alwaysRevert=*/false);
    suite2.add(std::move(standardScenarios().front()));
    suite2.add(intentionallyFailingScenario());
    auto r2 = suite2.runAll();
    CHECK(r2.at(0).passed && !r2.at(0).reverted, "TH1 DAT -> giu nguyen trang thai");
    CHECK(!r2.at(1).passed && r2.at(1).reverted, "TH8 KHONG DAT -> revertState");
    CHECK(sb2.balance(kChainA, kAlice) == 900 && sb2.balance(kChainB, "alice_b") == 100,
          "TH8 quay ve dung luc truoc TH8 (ket qua TH1 van con)");

    std::cout << "F. Test bi loi giua chung cung duoc revert\n";
    LambdaTestCase broken("loi", "goi chain khong ton tai",
                          [](Sandbox& s) { s.setBalance(kChainA, kAlice, 1000); },
                          [](Sandbox& s) { s.lock("ChainX", kChainB, kAlice, 1, "x"); }, nullptr);
    auto rb = broken.run(sb);
    CHECK(!rb.passed && rb.reverted && rb.error.find("ChainX") != std::string::npos, "loi duoc ghi lai, test KHONG DAT");
    CHECK(sb.stateView().empty(), "so du dat o Arrange da duoc tra ve");

    std::cout << "\n" << passes << " dat, " << failures << " khong dat\n";
    return failures == 0 ? 0 : 1;
}
