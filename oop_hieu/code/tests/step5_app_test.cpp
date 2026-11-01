// =====================================================================
//  Bước 5 — ứng dụng thực tế (Token Bridge + Lending), FlowTrace,
//  xuất báo cáo JSON và ghi SQLite.
// =====================================================================
#include "cc/report/ReportExporter.h"
#include "cc/testing/Scenarios.h"
#ifdef CC_HAVE_SQLITE
#include "cc/report/ReportRepository.h"
#include <sqlite3.h>
#endif

#include <cstdio>
#include <iostream>
#include <map>

using namespace cc;
using namespace cc::scenarios;

static int failures = 0, passes = 0;
#define CHECK(cond, name)                                                         \
    do {                                                                          \
        if (cond) { std::cout << "  [PASS] " << name << "\n"; ++passes; }         \
        else { std::cout << "  [FAIL] " << name << "\n"; ++failures; }            \
    } while (0)

static const TestReport& byName(const std::vector<TestReport>& rs, const std::string& n) {
    for (const auto& r : rs) if (r.name == n) return r;
    throw std::runtime_error("khong co kich ban " + n);
}
static bool flowHas(const TestReport& r, const std::string& part, const std::string& status = "") {
    for (const auto& f : r.flow)
        if (f.label.find(part) != std::string::npos && (status.empty() || f.status == status)) return true;
    return false;
}

#ifdef CC_HAVE_SQLITE
static long long count(sqlite3* db, const char* sql) {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db, sql, -1, &st, nullptr);
    long long n = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int64(st, 0) : -1;
    sqlite3_finalize(st);
    return n;
}
#endif

int main() {
    Sandbox sb;
    buildStandardWorld(sb);
    TestSuite suite(sb);
    for (auto& t : allScenarios(true)) suite.add(std::move(t));
    const auto reports = suite.runAll();

    // ------------------------------------------------------------------
    std::cout << "A. Ung dung thuc te: Token Bridge + Lending\n";
    std::map<std::string, int> perCat, passCat;
    for (const auto& r : reports) { perCat[r.category]++; passCat[r.category] += r.passed; }
    CHECK(reports.size() == 17, "17 kich ban (9 Token Bridge, 7 Lending, 1 kiem tra RevertState)");
    CHECK(perCat["Token Bridge"] == 9 && passCat["Token Bridge"] == 9, "9/9 kich ban Token Bridge DAT");
    CHECK(perCat["Lending"] == 7 && passCat["Lending"] == 7, "7/7 kich ban Lending DAT");
    CHECK(!byName(reports, "TH8_co_y_sai").passed, "TH8 co y sai van KHONG DAT (dung y do)");
    bool allReverted = true;
    for (const auto& r : reports) allReverted = allReverted && r.reverted;
    CHECK(allReverted && sb.stateView().empty(), "moi kich ban deu revertState, the gioi sach sau khi chay");

    std::cout << "B. Bridge pattern: them LendingBridge / UnlockBridge khong sua tang duoi\n";
    CHECK(sb.routeKind(kLendA, kLendS) == BridgeKind::Lending && sb.routeKind(kLendS, kLendA) == BridgeKind::Unlock &&
          sb.routeKind(kChainA, kChainB) == BridgeKind::Token, "3 loai bridge chay tren cung bo Adapter/Engine");

    // ------------------------------------------------------------------
    std::cout << "C. FlowTrace — luong thu cho so do tuan tu\n";
    const TestReport th1 = byName(reports, "TH1_thanh_cong");
    CHECK(th1.flow.size() == 4 && th1.flow[0].label == "lock 100" && th1.flow[1].label.find("Locked #0") != std::string::npos &&
          th1.flow[2].to == "Validator" && th1.flow[3].to == kChainB, "TH1: lock -> Locked -> kiem chu ky -> mint, dung thu tu");
    const TestReport th7 = byName(reports, "TH7_chain_dich_hong");
    CHECK(flowHas(th7, "chờ thử lại") && flowHas(th7, "hoàn 70", "ok") && flowHas(th7, "mint 70", "fail"),
          "TH7: co buoc loi, cho thu lai va hoan tien");
    const TestReport l3 = byName(reports, "L3_tra_no_nhan_lai_the_chap");
    CHECK(flowHas(l3, "repay 400", "ok") && flowHas(l3, "CollateralReleased") && flowHas(l3, "unlock 1000", "ok"),
          "L3: tra no -> su kien tra the chap -> unlock o EVM");
    CHECK(!byName(reports, "L2_vay_vuot_han_muc").relays.empty() &&
          byName(reports, "L2_vay_vuot_han_muc").relays.at(0).record.status == RelayStatus::REFUNDED,
          "bao cao giu lai thu cua relayer (chup truoc khi revert)");

    // ------------------------------------------------------------------
    std::cout << "D. Xuat bao cao cho dashboard\n";
    const std::string json = ReportExporter::toJson(sb, reports);
    CHECK(json.front() == '{' && json.find("\"reports\"") != std::string::npos && json.find("\"world\"") != std::string::npos &&
          json.find("L5_thanh_ly_khi_gia_giam") != std::string::npos, "JSON co du world, reports, flow");
    CHECK(ReportExporter::escape("a\"b\\c\n") == "\"a\\\"b\\\\c\\n\"", "escape ky tu dac biet dung chuan JSON");

#ifdef CC_HAVE_SQLITE
    std::cout << "E. Ghi vao crosschain.db (schema thiet ke tu dau)\n";
    const std::string dbPath = std::string(CC_BINARY_DIR) + "/step5_test.db";
    std::remove(dbPath.c_str());
    {
        ReportRepository repo(dbPath, std::string(CC_SOURCE_DIR) + "/data/schema.sql");
        repo.saveRun(sb, reports);
        repo.saveRun(sb, reports);   // chạy lần 2: test_cases giữ nguyên, test_runs tăng
    }
    sqlite3* db = nullptr;
    sqlite3_open(dbPath.c_str(), &db);
    CHECK(count(db, "SELECT count(*) FROM test_cases") == 17 && count(db, "SELECT count(*) FROM test_runs") == 34,
          "17 test_cases, 34 test_runs sau 2 lan chay");
    CHECK(count(db, "SELECT count(*) FROM assertion_results") > 100 && count(db, "SELECT count(*) FROM state_changes") > 30,
          "co assertion_results va state_changes");
    CHECK(count(db, "SELECT count(*) FROM cross_chain_messages WHERE status='EXECUTED'") > 5, "cross_chain_messages co thu EXECUTED");
    CHECK(count(db, "SELECT count(*) FROM v_test_summary WHERE total_runs = 2") == 17, "view v_test_summary thong ke dung");
    CHECK(count(db, "PRAGMA foreign_key_check") == -1, "khong vi pham khoa ngoai");
    sqlite3_close(db);
    std::remove(dbPath.c_str());
#else
    std::cout << "E. (bo qua: build khong co SQLite3)\n";
#endif

    std::cout << "\n" << passes << " dat, " << failures << " khong dat\n";
    return failures == 0 ? 0 : 1;
}
