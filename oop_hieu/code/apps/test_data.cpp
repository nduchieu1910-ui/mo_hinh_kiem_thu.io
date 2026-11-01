// =====================================================================
//  DATA TEST — chạy từng bộ dữ liệu kiểm thử (hợp lệ / biên / không hợp lệ)
//  theo đúng các trường hợp TH1..TH10 và L1..L7 của đồ án.
//
//  Mỗi bộ dữ liệu:  Snapshot -> Arrange -> Act -> đọc số dư -> so với
//  số dư MONG ĐỢI ghi sẵn trong bảng -> RevertState (bộ sau không bị ảnh hưởng).
//
//  Chạy:   ./build/test_data            in bảng số dư từng bộ dữ liệu
//          ./build/test_data --csv f    ghi thêm kết quả ra file CSV
//          ./build/test_data -q         chỉ in dòng tổng kết từng bộ
// =====================================================================
#include "cc/bridge/MultiSigProofValidator.h"
#include "cc/core/HashUtil.h"
#include "cc/testing/Scenarios.h"

#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace cc;
using namespace cc::scenarios;

namespace {

struct Acc { std::string chain, addr, label; };

struct DataCase {
    std::string id, group, kind, title, packet, input;
    std::vector<Acc> accounts;
    std::vector<Amount> expected;            // số dư mong đợi sau khi chạy, cùng thứ tự accounts
    std::string expectedStatus;              // trạng thái mong đợi (chuỗi con phải xuất hiện)
    std::function<void(Sandbox&)> arrange;
    std::function<std::string(Sandbox&)> act; // trả về trạng thái thực tế
};

// ---------- tiện ích ----------
const Address kBob = "0xb0b", kCarol = "0xca201";
Address liqSol() { return solAddr("Liquidatr1"); }
const char* kLiqHome = "0x11d";

std::string relayState(Sandbox& sb, const std::string& s, const std::string& d, std::size_t i = 0) {
    const auto& recs = sb.relayRecords(s, d);
    if (recs.size() <= i) return "không có thư";
    const auto& r = recs.at(i);
    return std::string(toString(r.status)) + " (" + std::to_string(r.attempts) + " lần thử)";
}
std::string txState(const TransactionResult& r) {
    std::string s = toString(r.status);
    if (!r.ok() && !r.revertReason.empty()) s += " — " + r.revertReason;
    return s;
}
std::string storage(Sandbox& sb, const std::string& chain, const Address& c, const std::string& slot) {
    auto l = sb.ledger(chain);
    if (!l) return "";
    auto st = l->getContract(c);
    if (!st) return "";
    auto v = st->get(slot);
    return v ? *v : "";
}

// ---------- các nhóm thế giới thường dùng ----------
const std::vector<Acc> kTokAB = {{kChainA, kAlice, "Alice (ChainA-EVM)"}, {kChainA, kEscrow, "Két bridge (ChainA)"}, {kChainB, "alice_b", "alice_b (ChainB-UTXO)"}};
std::vector<Acc> accRecv(const std::string& recv) { return {{kChainA, kAlice, "Alice (ChainA-EVM)"}, {kChainA, kEscrow, "Két bridge (ChainA)"}, {kChainB, recv, recv + " (ChainB-UTXO)"}}; }
const std::vector<Acc> kTokAM = {{kChainA, kAlice, "Alice (ChainA-EVM)"}, {kChainA, kEscrow, "Két bridge (ChainA)"}, {kChainM, "bob", "bob (ChainM-Mock)"}};
const std::vector<Acc> kTokAS = {{kChainA, kAlice, "Alice (ChainA-EVM)"}, {kChainA, kEscrow, "Két bridge (ChainA)"}, {kChainS, "", "Alice (ChainS-Solana)"}};
const std::vector<Acc> kLend = {{kLendA, kAlice, "Alice (LendA-EVM)"}, {kLendA, kEscrow, "Két thế chấp (LendA)"}, {kLendS, "", "Alice (LendS-Solana)"}};

void setupPool(Sandbox& sb) {
    sb.setBalance(kLendA, kAlice, 5000);
    sb.setBalance(kLendS, kSolVault(), 100000);
    sb.setStorage(kLendS, kSolVault(), "priceX100", "100");
    sb.setStorage(kLendS, kSolVault(), "ltvBps", "5000");
    sb.setStorage(kLendS, kSolVault(), "liqBps", "8000");
}
void borrow(Sandbox& sb, Amount coll, Amount loan) {
    sb.lock(kLendA, kLendS, kAlice, coll, kAliceSol(), "borrow=" + amountToString(loan));
    sb.relayAll();
}
std::string debtKey() { return "debt[" + kAliceSol() + "]"; }

// lock + relay, trả về trạng thái thư đầu tiên (hoặc lỗi ở bước khóa)
std::function<std::string(Sandbox&)> lockRelay(const std::string& s, const std::string& d, Address from, Amount amt, Address to) {
    return [=](Sandbox& sb) {
        auto r = sb.lock(s, d, from, amt, to);
        if (!r.ok()) return "khóa: " + txState(r);
        sb.relayAll();
        return relayState(sb, s, d);
    };
}
std::function<void(Sandbox&)> alice(Amount v) { return [=](Sandbox& sb) { sb.setBalance(kChainA, kAlice, v); }; }

std::vector<DataCase> buildCases() {
    std::vector<DataCase> v;
    // ======================= TH1 — chuyển thành công =======================
    v.push_back({"D01", "TH1", "Hợp lệ", "Chuyển 100 từ EVM sang UTXO", "MSG-TH1-001", "Alice 1000; khóa 100 → alice_b",
                 kTokAB, {900, 100, 100}, "EXECUTED", alice(1000), lockRelay(kChainA, kChainB, kAlice, 100, "alice_b")});
    v.push_back({"D02", "TH1", "Biên dưới", "Chuyển số tiền nhỏ nhất (1)", "MSG-TH1-002", "Alice 1000; khóa 1 → alice_b",
                 kTokAB, {999, 1, 1}, "EXECUTED", alice(1000), lockRelay(kChainA, kChainB, kAlice, 1, "alice_b")});
    v.push_back({"D03", "TH1", "Biên trên", "Chuyển toàn bộ số dư", "MSG-TH1-003", "Alice 1000; khóa 1000 → alice_b",
                 kTokAB, {0, 1000, 1000}, "EXECUTED", alice(1000), lockRelay(kChainA, kChainB, kAlice, 1000, "alice_b")});
    v.push_back({"D04", "TH1", "Hợp lệ", "Chuyển EVM → Solana", "MSG-TH1-004", "Alice 1000; khóa 100 → Alice trên Solana",
                 kTokAS, {900, 100, 100}, "EXECUTED", alice(1000), lockRelay(kChainA, kChainS, kAlice, 100, kAliceSol())});
    v.push_back({"D05", "TH1", "Hợp lệ", "Chuyển EVM → Mock", "MSG-TH1-005", "Alice 1000; khóa 100 → bob trên ChainM",
                 kTokAM, {900, 100, 100}, "EXECUTED", alice(1000), lockRelay(kChainA, kChainM, kAlice, 100, "bob")});

    // ======================= TH2 — thư giả / Proof Validation =======================
    auto forged = [](std::function<void(CrossChainMessage&)> f) {
        return [=](Sandbox& sb) {
            CrossChainMessage m = sb.makeMessage(kChainA, kChainB, kAlice, "alice_b", 100, 99);
            f(m);
            return txState(sb.injectMessage(kChainA, kChainB, m));
        };
    };
    v.push_back({"D06", "TH2", "Không hợp lệ", "Thư do kẻ gian tự ký, đòi phát 1.000.000", "MSG-TH2-006", "proof = chữ ký của hacker_1, hacker_2; amount 1.000.000",
                 kTokAB, {1000, 0, 0}, "không đủ chữ ký", alice(1000),
                 forged([](CrossChainMessage& m) { m.amount = 1000000; m.messageHash = m.computeHash(); m.proof = MultiSigProofValidator::attest(m, {"hacker_1", "hacker_2"}); })});
    v.push_back({"D07", "TH2", "Không hợp lệ", "Thư chỉ có 1/3 chữ ký hợp lệ", "MSG-TH2-007", "proof = chữ ký của val_a",
                 kTokAB, {1000, 0, 0}, "không đủ chữ ký", alice(1000),
                 forged([](CrossChainMessage& m) { m.proof = MultiSigProofValidator::attest(m, {"val_a"}); })});
    v.push_back({"D08", "TH2", "Không hợp lệ", "Thư không có proof", "MSG-TH2-008", "proof = rỗng",
                 kTokAB, {1000, 0, 0}, "FAILED", alice(1000), forged([](CrossChainMessage& m) { m.proof = ""; })});
    v.push_back({"D09", "TH2", "Không hợp lệ", "Sửa số tiền sau khi validator đã ký", "MSG-TH2-009", "ký với amount 100, sửa thành 1.000.000",
                 kTokAB, {1000, 0, 0}, "FAILED", alice(1000), forged([](CrossChainMessage& m) { m.amount = 1000000; })});
    v.push_back({"D10", "TH2", "Không hợp lệ", "Một validator ký hai lần", "MSG-TH2-010", "proof = val_a, val_a (chỉ tính 1)",
                 kTokAB, {1000, 0, 0}, "không đủ chữ ký", alice(1000),
                 forged([](CrossChainMessage& m) { m.proof = MultiSigProofValidator::attest(m, {"val_a", "val_a"}); })});
    v.push_back({"D11", "TH2", "Biên", "Một validator nghỉ, còn 2/3 ký", "MSG-TH2-011", "validator hoạt động: val_a, val_b; khóa 100",
                 kTokAB, {900, 100, 100}, "EXECUTED",
                 [](Sandbox& sb) { sb.setBalance(kChainA, kAlice, 1000); sb.setActiveSigners(kChainA, kChainB, {"val_a", "val_b"}); },
                 lockRelay(kChainA, kChainB, kAlice, 100, "alice_b")});
    v.push_back({"D12", "TH2", "Không hợp lệ", "Hai validator sập, còn 1/3 ký", "MSG-TH2-012", "validator hoạt động: val_a; khóa 100",
                 kTokAB, {900, 100, 0}, "REJECTED",
                 [](Sandbox& sb) { sb.setBalance(kChainA, kAlice, 1000); sb.setActiveSigners(kChainA, kChainB, {"val_a"}); },
                 lockRelay(kChainA, kChainB, kAlice, 100, "alice_b")});

    // ======================= TH3 — gửi lại thư cũ =======================
    auto sent100 = [](Sandbox& sb) { sb.setBalance(kChainA, kAlice, 1000); sb.lock(kChainA, kChainB, kAlice, 100, "alice_b"); sb.relayAll(); };
    v.push_back({"D13", "TH3", "Không hợp lệ", "Gửi lại nguyên thư nonce 0 đã thực thi", "MSG-TH1-001 (lần 2)", "bản sao thư nonce 0",
                 kTokAB, {900, 100, 100}, "replay", sent100,
                 [](Sandbox& sb) { return txState(sb.injectMessage(kChainA, kChainB, sb.relayRecords(kChainA, kChainB).at(0).message)); }});
    v.push_back({"D14", "TH3", "Không hợp lệ", "Thư mới nội dung khác nhưng trùng nonce 0", "MSG-TH3-014", "nonce 0, amount 500, đủ chữ ký",
                 kTokAB, {900, 100, 100}, "replay", sent100,
                 [](Sandbox& sb) { return txState(sb.injectMessage(kChainA, kChainB, sb.makeMessage(kChainA, kChainB, kAlice, "alice_b", 500, 0))); }});
    v.push_back({"D15", "TH3", "Không hợp lệ", "Gửi lại thư cũ hai lần liên tiếp", "MSG-TH1-001 (lần 2, 3)", "bản sao thư nonce 0 × 2",
                 kTokAB, {900, 100, 100}, "replay", sent100,
                 [](Sandbox& sb) {
                     auto m = sb.relayRecords(kChainA, kChainB).at(0).message;
                     sb.injectMessage(kChainA, kChainB, m);
                     return txState(sb.injectMessage(kChainA, kChainB, m));
                 }});

    // ======================= TH4 — sai địa chỉ người nhận =======================
    v.push_back({"D16", "TH4", "Không hợp lệ", "Người nhận quá ngắn", "MSG-TH4-016", "người nhận \"abc\" (3 ký tự)",
                 accRecv("abc"), {1000, 0, 0}, "REFUNDED (3", alice(1000), lockRelay(kChainA, kChainB, kAlice, 100, "abc")});
    v.push_back({"D17", "TH4", "Không hợp lệ", "Người nhận có chữ hoa", "MSG-TH4-017", "người nhận \"Alice_B\"",
                 accRecv("Alice_B"), {1000, 0, 0}, "REFUNDED", alice(1000), lockRelay(kChainA, kChainB, kAlice, 100, "Alice_B")});
    v.push_back({"D18", "TH4", "Không hợp lệ", "Người nhận có khoảng trắng", "MSG-TH4-018", "người nhận \"alice b\"",
                 accRecv("alice b"), {1000, 0, 0}, "REFUNDED", alice(1000), lockRelay(kChainA, kChainB, kAlice, 100, "alice b")});
    v.push_back({"D19", "TH4", "Biên", "Người nhận 5 ký tự (dưới mức tối thiểu 6)", "MSG-TH4-019", "người nhận \"abcde\"",
                 accRecv("abcde"), {1000, 0, 0}, "REFUNDED", alice(1000), lockRelay(kChainA, kChainB, kAlice, 100, "abcde")});
    v.push_back({"D20", "TH4", "Biên", "Người nhận đúng 6 ký tự (mức tối thiểu)", "MSG-TH4-020", "người nhận \"abcdef\"",
                 accRecv("abcdef"), {900, 100, 100}, "EXECUTED", alice(1000), lockRelay(kChainA, kChainB, kAlice, 100, "abcdef")});

    // ======================= TH5 — không đủ tiền =======================
    v.push_back({"D21", "TH5", "Không hợp lệ", "Số dư 50, khóa 100", "MSG-TH5-021", "Alice 50; khóa 100",
                 kTokAB, {50, 0, 0}, "REVERTED", alice(50), lockRelay(kChainA, kChainB, kAlice, 100, "alice_b")});
    v.push_back({"D22", "TH5", "Biên", "Khóa nhiều hơn số dư đúng 1 đơn vị", "MSG-TH5-022", "Alice 100; khóa 101",
                 kTokAB, {100, 0, 0}, "REVERTED", alice(100), lockRelay(kChainA, kChainB, kAlice, 101, "alice_b")});
    v.push_back({"D23", "TH5", "Biên", "Khóa đúng bằng số dư", "MSG-TH5-023", "Alice 100; khóa 100",
                 kTokAB, {0, 100, 100}, "EXECUTED", alice(100), lockRelay(kChainA, kChainB, kAlice, 100, "alice_b")});
    v.push_back({"D24", "TH5", "Không hợp lệ", "Tài khoản trống, khóa 1", "MSG-TH5-024", "Alice 0; khóa 1",
                 kTokAB, {0, 0, 0}, "REVERTED", alice(0), lockRelay(kChainA, kChainB, kAlice, 1, "alice_b")});
    v.push_back({"D25", "TH5", "Không hợp lệ", "Số tiền bằng 0", "MSG-TH5-025", "Alice 1000; khóa 0",
                 kTokAB, {1000, 0, 0}, "FAILED", alice(1000), lockRelay(kChainA, kChainB, kAlice, 0, "alice_b")});

    // ======================= TH6 — tiêu một tờ hai lần (UTXO) =======================
    const std::vector<Acc> kUtxo = {{kChainB, "alice_b", "alice_b (ChainB-UTXO)"}, {kChainB, "dave_bb", "dave_bb (ChainB-UTXO)"}, {kChainB, "erin_bb", "erin_bb (ChainB-UTXO)"}};
    auto pay = [](const std::string& id, std::vector<TxOutput> outs, const std::string& signer, const std::string& h) {
        UtxoTransaction t;
        t.inputs = {{id, simSign(signer, id)}};
        t.outputs = std::move(outs);
        t.txHash = h;
        return t;
    };
    v.push_back({"D26", "TH6", "Không hợp lệ", "Dùng cùng tờ 100 trả cho hai người", "TX-TH6-026", "tờ genesis1:0 (100): trả dave_bb 100, rồi trả erin_bb 100",
                 kUtxo, {0, 100, 0}, "lần 2: REVERTED", [](Sandbox& sb) { sb.giveUtxo(kChainB, "alice_b", 100); },
                 [pay](Sandbox& sb) {
                     auto id = sb.ledger(kChainB)->unspentOf("alice_b").at(0).id;
                     auto r1 = sb.submitRaw(kChainB, pay(id, {{"dave_bb", 100}}, "alice_b", "p1"));
                     auto r2 = sb.submitRaw(kChainB, pay(id, {{"erin_bb", 100}}, "alice_b", "p2"));
                     return "lần 1: " + txState(r1) + "; lần 2: " + txState(r2);
                 }});
    v.push_back({"D27", "TH6", "Không hợp lệ", "Tiêu tờ của người khác (chữ ký sai chủ)", "TX-TH6-027", "mallory ký tờ của alice_b, trả dave_bb 100",
                 kUtxo, {100, 0, 0}, "REVERTED", [](Sandbox& sb) { sb.giveUtxo(kChainB, "alice_b", 100); },
                 [pay](Sandbox& sb) { auto id = sb.ledger(kChainB)->unspentOf("alice_b").at(0).id; return txState(sb.submitRaw(kChainB, pay(id, {{"dave_bb", 100}}, "mallory", "p3"))); }});
    v.push_back({"D28", "TH6", "Không hợp lệ", "Tổng tờ ra lớn hơn tờ vào", "TX-TH6-028", "tờ 100, chi 150 cho dave_bb",
                 kUtxo, {100, 0, 0}, "REVERTED", [](Sandbox& sb) { sb.giveUtxo(kChainB, "alice_b", 100); },
                 [pay](Sandbox& sb) { auto id = sb.ledger(kChainB)->unspentOf("alice_b").at(0).id; return txState(sb.submitRaw(kChainB, pay(id, {{"dave_bb", 150}}, "alice_b", "p4"))); }});
    v.push_back({"D29", "TH6", "Hợp lệ", "Chi một phần, nhận tiền thối", "TX-TH6-029", "tờ 100: dave_bb 60, thối alice_b 40",
                 kUtxo, {40, 60, 0}, "SUCCESS", [](Sandbox& sb) { sb.giveUtxo(kChainB, "alice_b", 100); },
                 [pay](Sandbox& sb) { auto id = sb.ledger(kChainB)->unspentOf("alice_b").at(0).id; return txState(sb.submitRaw(kChainB, pay(id, {{"dave_bb", 60}, {"alice_b", 40}}, "alice_b", "p5"))); }});

    // ======================= TH7 — chain đích lỗi, thử lại, hoàn tiền =======================
    auto down = [](int n) { return [n](Sandbox& sb) { sb.setBalance(kChainA, kAlice, 1000); if (n) sb.failNext(kChainM, TxStatus::REVERTED, "hết gas", n); }; };
    v.push_back({"D30", "TH7", "Hợp lệ", "Chain đích lỗi 1 lần rồi chạy lại", "MSG-TH7-030", "ChainM hết gas 1 lần; khóa 70 → bob",
                 kTokAM, {930, 70, 70}, "EXECUTED (2", down(1), lockRelay(kChainA, kChainM, kAlice, 70, "bob")});
    v.push_back({"D31", "TH7", "Biên", "Lỗi 2 lần, thành công ở lần thử cuối (3)", "MSG-TH7-031", "ChainM hết gas 2 lần",
                 kTokAM, {930, 70, 70}, "EXECUTED (3", down(2), lockRelay(kChainA, kChainM, kAlice, 70, "bob")});
    v.push_back({"D32", "TH7", "Biên", "Lỗi đúng 3 lần, hết lượt thử", "MSG-TH7-032", "ChainM hết gas 3 lần",
                 kTokAM, {1000, 0, 0}, "REFUNDED (3", down(3), lockRelay(kChainA, kChainM, kAlice, 70, "bob")});
    v.push_back({"D33", "TH7", "Không hợp lệ", "Chain đích hỏng hẳn", "MSG-TH7-033", "ChainM hết gas 5 lần",
                 kTokAM, {1000, 0, 0}, "REFUNDED (3", down(5), lockRelay(kChainA, kChainM, kAlice, 70, "bob")});

    // ======================= TH9 — chuyển khứ hồi =======================
    auto roundTrip = [](Amount back) {
        return [back](Sandbox& sb) {
            sb.lock(kChainA, kChainS, kAlice, 100, kAliceSol());
            sb.relayAll();
            auto r = sb.lock(kChainS, kChainA, kAliceSol(), back, kAlice);
            if (!r.ok()) return "chiều đi: " + relayState(sb, kChainA, kChainS) + "; chiều về: khóa " + txState(r);
            sb.relayAll();
            return "chiều đi: " + relayState(sb, kChainA, kChainS) + "; chiều về: " + relayState(sb, kChainS, kChainA);
        };
    };
    v.push_back({"D34", "TH9", "Hợp lệ", "Đi 100, về 100", "MSG-TH9-034", "Alice 1000; A → S 100; S → A 100",
                 kTokAS, {1000, 0, 0}, "chiều về: EXECUTED", alice(1000), roundTrip(100)});
    v.push_back({"D35", "TH9", "Hợp lệ", "Đi 100, về 40", "MSG-TH9-035", "Alice 1000; A → S 100; S → A 40",
                 kTokAS, {940, 60, 60}, "chiều về: EXECUTED", alice(1000), roundTrip(40)});
    v.push_back({"D36", "TH9", "Không hợp lệ", "Đi 100, đòi về 150", "MSG-TH9-036", "Alice 1000; A → S 100; S → A 150",
                 kTokAS, {900, 100, 100}, "REVERTED", alice(1000), roundTrip(150)});

    // ======================= TH10 — nhiều người cùng chuyển =======================
    const std::vector<Acc> kMulti = {{kChainA, kEscrow, "Két bridge (ChainA)"}, {kChainB, "alice_b", "alice_b (ChainB)"},
                                      {kChainB, "bob_bbb", "bob_bbb (ChainB)"}, {kChainB, "carol_b", "carol_b (ChainB)"}};
    auto multi = [](Amount carolHas) {
        return [carolHas](Sandbox& sb) { sb.setBalance(kChainA, kAlice, 1000); sb.setBalance(kChainA, kBob, 500); sb.setBalance(kChainA, kCarol, carolHas); };
    };
    auto multiAct = [](Sandbox& sb) {
        sb.lock(kChainA, kChainB, kAlice, 100, "alice_b");
        sb.lock(kChainA, kChainB, kBob, 200, "bob_bbb");
        auto rc = sb.lock(kChainA, kChainB, kCarol, 300, "carol_b");
        sb.relayAll();
        std::string s = std::to_string(sb.relayRecords(kChainA, kChainB).size()) + " thư";
        for (std::size_t i = 0; i < sb.relayRecords(kChainA, kChainB).size(); ++i)
            s += "; #" + std::to_string(i) + " " + toString(sb.relayRecords(kChainA, kChainB).at(i).status);
        if (!rc.ok()) s += "; Carol khóa: " + std::string(toString(rc.status));
        return s;
    };
    v.push_back({"D37", "TH10", "Hợp lệ", "Ba người cùng chuyển", "MSG-TH10-037..039", "Alice 100, Bob 200, Carol 300",
                 kMulti, {600, 100, 200, 300}, "3 thư", multi(300), multiAct});
    v.push_back({"D38", "TH10", "Không hợp lệ", "Ba người chuyển, Carol thiếu tiền", "MSG-TH10-040..042", "Carol chỉ có 100 nhưng chuyển 300",
                 kMulti, {300, 100, 200, 0}, "Carol khóa: REVERTED", multi(100), multiAct});

    // ======================= TH8 — cố ý sai (kiểm tra RevertState) =======================
    v.push_back({"D39", "TH8", "Cố ý sai", "Mong đợi sai có chủ đích: alice_b 999", "MSG-TH8-039", "giống D01 nhưng mong đợi alice_b = 999",
                 kTokAB, {900, 100, 999}, "EXECUTED", alice(1000), lockRelay(kChainA, kChainB, kAlice, 100, "alice_b")});

    // ======================= L1/L2 — vay =======================
    auto lendAct = [](Amount coll, Amount loan) {
        return [coll, loan](Sandbox& sb) { borrow(sb, coll, loan); return relayState(sb, kLendA, kLendS) + "; nợ = " + storage(sb, kLendS, kSolVault(), debtKey()); };
    };
    v.push_back({"D40", "L1", "Hợp lệ", "Thế chấp 1000, vay 400 (tối đa 500)", "MSG-L1-040", "priceX100 100, LTV 50%; borrow=400",
                 kLend, {4000, 1000, 400}, "EXECUTED", setupPool, lendAct(1000, 400)});
    v.push_back({"D41", "L1", "Biên", "Vay đúng hạn mức 500", "MSG-L1-041", "borrow=500",
                 kLend, {4000, 1000, 500}, "EXECUTED", setupPool, lendAct(1000, 500)});
    v.push_back({"D42", "L2", "Biên", "Vay vượt hạn mức 1 đơn vị (501)", "MSG-L2-042", "borrow=501",
                 kLend, {5000, 0, 0}, "REFUNDED", setupPool, lendAct(1000, 501)});
    v.push_back({"D43", "L2", "Không hợp lệ", "Vay 700 (vượt xa hạn mức)", "MSG-L2-043", "borrow=700",
                 kLend, {5000, 0, 0}, "REFUNDED", setupPool, lendAct(1000, 700)});

    // ======================= L3/L4 — trả nợ =======================
    auto repay = [](Amount amt) {
        return [amt](Sandbox& sb) {
            auto r = sb.send(kLendS, {"repay", kAliceSol(), "", amt, {}});
            sb.relayAll();
            return "trả nợ: " + txState(r) + "; nợ còn = " + storage(sb, kLendS, kSolVault(), debtKey());
        };
    };
    auto loan400 = [](Sandbox& sb) { setupPool(sb); borrow(sb, 1000, 400); };
    v.push_back({"D44", "L3", "Hợp lệ", "Trả đủ 400: nhận lại thế chấp", "MSG-L3-044", "repay 400",
                 kLend, {5000, 0, 0}, "nợ còn = 0", loan400, repay(400)});
    v.push_back({"D45", "L4", "Hợp lệ", "Trả một nửa (200/400)", "MSG-L4-045", "repay 200",
                 kLend, {4000, 1000, 200}, "nợ còn = 200", loan400, repay(200)});
    v.push_back({"D46", "L4", "Biên", "Trả 399, còn nợ 1: chưa nhả thế chấp", "MSG-L4-046", "repay 399",
                 kLend, {4000, 1000, 1}, "nợ còn = 1", loan400, repay(399)});
    v.push_back({"D47", "L4", "Không hợp lệ", "Trả nhiều hơn số nợ", "MSG-L4-047", "repay 500 khi nợ 400",
                 kLend, {4000, 1000, 400}, "REVERTED", loan400, repay(500)});

    // ======================= L5/L7 — thanh lý =======================
    const std::vector<Acc> kLiq = {{kLendA, kLiqHome, "Người thanh lý (LendA)"}, {kLendA, kEscrow, "Két thế chấp (LendA)"}, {kLendS, "", "Người thanh lý (LendS)"}};
    auto liquidate = [](const std::string& price, Amount pay) {
        return [price, pay](Sandbox& sb) {
            sb.setStorage(kLendS, kSolVault(), "priceX100", price);
            auto r = sb.send(kLendS, {"liquidate", liqSol(), kAliceSol(), pay, {{"receiverOnHome", kLiqHome}}});
            sb.relayAll();
            return "thanh lý: " + txState(r) + "; nợ còn = " + storage(sb, kLendS, kSolVault(), debtKey());
        };
    };
    auto loanLiq = [](Sandbox& sb) { setupPool(sb); borrow(sb, 1000, 400); sb.setBalance(kLendS, liqSol(), 2000); };
    v.push_back({"D48", "L5", "Hợp lệ", "Giá giảm 60% (100 → 40): thanh lý", "TX-L5-048", "priceX100 40; ngưỡng 320 < nợ 400",
                 kLiq, {1000, 0, 1600}, "SUCCESS", loanLiq, liquidate("40", 400)});
    v.push_back({"D49", "L5", "Biên", "Giá 49: ngưỡng 392 < nợ 400, được thanh lý", "TX-L5-049", "priceX100 49",
                 kLiq, {1000, 0, 1600}, "SUCCESS", loanLiq, liquidate("49", 400)});
    v.push_back({"D50", "L7", "Biên", "Giá 50: ngưỡng 400 = nợ 400, chưa được thanh lý", "TX-L7-050", "priceX100 50",
                 kLiq, {0, 1000, 2000}, "REVERTED", loanLiq, liquidate("50", 400)});
    v.push_back({"D51", "L7", "Không hợp lệ", "Giá không đổi (100): khoản vay an toàn", "TX-L7-051", "priceX100 100; ngưỡng 800",
                 kLiq, {0, 1000, 2000}, "REVERTED", loanLiq, liquidate("100", 400)});
    v.push_back({"D52", "L7", "Không hợp lệ", "Giá 40 nhưng người thanh lý trả thiếu (300/400)", "TX-L7-052", "priceX100 40; trả 300",
                 kLiq, {0, 1000, 2000}, "REVERTED", loanLiq, liquidate("40", 300)});

    // ======================= L6 — gửi lại thư trả thế chấp =======================
    v.push_back({"D53", "L6", "Không hợp lệ", "Gửi lại thư CollateralReleased để rút thế chấp lần 2", "MSG-L6-053", "bản sao thư trả thế chấp đã thực thi",
                 kLend, {5000, 0, 0}, "replay",
                 [](Sandbox& sb) { setupPool(sb); borrow(sb, 1000, 400); sb.send(kLendS, {"repay", kAliceSol(), "", 400, {}}); sb.relayAll(); },
                 [](Sandbox& sb) { return txState(sb.injectMessage(kLendS, kLendA, sb.relayRecords(kLendS, kLendA).at(0).message)); }});
    return v;
}

Amount bal(Sandbox& sb, const Acc& a) {
    Address who = a.addr;
    if (who.empty()) who = a.label.find("thanh lý") != std::string::npos ? liqSol() : kAliceSol();
    return sb.balance(a.chain, who);
}

std::string flowOf(const std::string& g) {
    if (g == "TH1" || g == "TH4" || g == "TH5" || g == "TH8") return "Alice (ChainA) -> khóa vào két -> Relayer (2/3 chữ ký) -> phát cho người nhận ở chain đích";
    if (g == "TH2") return "Kẻ gian -> thư giả -> MultiSigProofValidator kiểm chữ ký -> CHẶN";
    if (g == "TH3") return "Thư cũ -> chain đích kiểm (chain nguồn, nonce) -> CHẶN replay";
    if (g == "TH6") return "alice_b (ChainB) -> tiêu tờ UTXO -> sổ cái kiểm tờ còn/chữ ký/tổng tiền";
    if (g == "TH7") return "Alice (ChainA) -> két -> Relayer -> ChainM lỗi -> thử lại tối đa 3 -> hoàn tiền";
    if (g == "TH9") return "Alice (ChainA) -> ChainS -> khóa ở ChainS -> Relayer -> nhả két ChainA";
    if (g == "TH10") return "Alice, Bob, Carol (ChainA) -> két -> Relayer (từng thư, nonce riêng) -> ChainB";
    if (g == "L1" || g == "L2") return "Alice (LendA) -> khóa thế chấp -> thư CollateralLocked -> quỹ LendS cho vay";
    if (g == "L3" || g == "L4" || g == "L6") return "Alice (LendS) -> trả nợ -> thư CollateralReleased -> nhả thế chấp ở LendA";
    return "Người thanh lý (LendS) -> trả nợ thay -> thư CollateralReleased -> nhận thế chấp ở LendA";
}

std::size_t u8len(const std::string& s) {
    std::size_t n = 0;
    for (unsigned char ch : s) if ((ch & 0xC0) != 0x80) ++n;
    return n;
}

std::string csvq(const std::string& s) {
    std::string o = "\"";
    for (char c : s) { if (c == '"') o += "\"\""; else o += c; }
    return o + "\"";
}

} // namespace

int main(int argc, char** argv) {
    std::string csvPath;
    bool quiet = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--csv" && i + 1 < argc) csvPath = argv[++i];
        else if (a == "-q") quiet = true;
    }
    Sandbox sb;
    buildStandardWorld(sb);
    auto cases = buildCases();

    std::ofstream csv;
    if (!csvPath.empty()) {
        csv.open(csvPath);
        csv << "\xEF\xBB\xBF" << "id,group,kind,title,packet,input,account,before,expected,actual,expected_status,actual_status,balance_ok,status_ok\n";
    }
    int pass = 0, fail = 0, intended = 0;
    for (auto& c : cases) {
        const SandboxSnapshot snap = sb.snapshot("data-" + c.id);
        c.arrange(sb);
        std::vector<Amount> before;
        for (auto& a : c.accounts) before.push_back(bal(sb, a));
        std::string status;
        try { status = c.act(sb); } catch (const std::exception& e) { status = std::string("LỖI: ") + e.what(); }
        std::vector<Amount> after;
        for (auto& a : c.accounts) after.push_back(bal(sb, a));
        sb.revertState(snap);

        bool balOk = after == c.expected;
        bool stOk = status.find(c.expectedStatus) != std::string::npos;
        bool ok = balOk && stOk;
        const bool isIntended = c.group == "TH8";
        if (ok) ++pass; else if (isIntended) ++intended; else ++fail;

        if (!quiet) {
            std::cout << "\n==================================================================\n"
                      << ">>> " << c.id << " | " << c.group << " | " << c.kind << " | " << c.title << "\n"
                      << "    Luồng: " << flowOf(c.group) << "\n"
                      << "    Gói tin: " << c.packet << "   Dữ liệu: " << c.input << "\n"
                      << "------------------------------------------------------------------\n"
                      << "    Trạng thái thực tế: " << status << "\n\n"
                      << "    +---------------------------+------------+------------+------------+\n"
                      << "    | Tài khoản                 |  Ban đầu   |  Mong đợi  |  Thực tế   |\n"
                      << "    +---------------------------+------------+------------+------------+\n";
            for (std::size_t i = 0; i < c.accounts.size(); ++i) {
                std::string lab = c.accounts[i].label;
                std::size_t w = u8len(lab);
                std::cout << "    | " << lab << std::string(w < 26 ? 26 - w : 1, ' ')
                          << "| " << std::setw(10) << amountToString(before[i]) << " | " << std::setw(10) << amountToString(c.expected[i])
                          << " | " << std::setw(10) << amountToString(after[i]) << " |\n";
            }
            std::cout << "    +---------------------------+------------+------------+------------+\n";
        }
        std::cout << (quiet ? "" : "    ") << (ok ? "[ĐẠT]      " : (isIntended ? "[CỐ Ý SAI] " : "[KHÔNG ĐẠT]")) << " " << c.id << " " << c.group << " — " << c.title << "\n";

        if (csv) {
            for (std::size_t i = 0; i < c.accounts.size(); ++i)
                csv << c.id << ',' << c.group << ',' << csvq(c.kind) << ',' << csvq(c.title) << ',' << csvq(c.packet) << ',' << csvq(c.input) << ','
                    << csvq(c.accounts[i].label) << ',' << amountToString(before[i]) << ',' << amountToString(c.expected[i]) << ',' << amountToString(after[i]) << ','
                    << csvq(c.expectedStatus) << ',' << csvq(status) << ',' << (balOk ? 1 : 0) << ',' << (stOk ? 1 : 0) << '\n';
        }
    }
    std::cout << "\nTỔNG KẾT: " << cases.size() << " bộ dữ liệu — " << pass << " đạt, " << fail << " không đạt, "
              << intended << " cố ý sai (TH8)\n";
    return fail == 0 ? 0 : 1;
}
