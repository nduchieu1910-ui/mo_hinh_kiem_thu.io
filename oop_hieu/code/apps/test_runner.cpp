// =====================================================================
//  CLI TEST RUNNER (Bước 5)
//
//  Chạy không tham số  -> menu tương tác: chọn kịch bản, xem luồng thư
//                         và bảng thay đổi trạng thái.
//  Tham số:
//    --list                 liệt kê kịch bản
//    --run all | 1,3,12     chạy các kịch bản (theo số thứ tự)
//    --json <file.json>     xuất báo cáo JSON (+ file .js cạnh nó cho dashboard)
//    --db <crosschain.db>   ghi kết quả vào SQLite (cần build có SQLite3)
//    --schema <schema.sql>  schema dùng khi database còn trống (mặc định data/schema.sql)
//    -q                     chỉ in bảng tổng kết
//
//  Windows: chạy "chcp 65001" trước để terminal hiện đúng tiếng Việt.
// =====================================================================
#include "cc/report/ReportExporter.h"
#include "cc/testing/Scenarios.h"
#ifdef CC_HAVE_SQLITE
#include "cc/report/ReportRepository.h"
#endif

#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace cc;

namespace {

struct Runner {
    Sandbox                                sb;
    std::vector<std::unique_ptr<TestCase>> cases;
    std::vector<TestReport>                lastReports;

    Runner() {
        scenarios::buildStandardWorld(sb);
        cases = scenarios::allScenarios(true);
    }

    std::vector<TestReport> run(const std::vector<std::size_t>& idx) {
        std::vector<TestReport> out;
        for (std::size_t i : idx) out.push_back(cases.at(i)->run(sb));
        lastReports = out;
        return out;
    }
    std::vector<std::size_t> all() const {
        std::vector<std::size_t> v;
        for (std::size_t i = 0; i < cases.size(); ++i) v.push_back(i);
        return v;
    }
};

std::string shorten(const std::string& s, std::size_t n) {
    return s.size() <= n ? s : s.substr(0, 8) + "…" + s.substr(s.size() - 4);
}

void printList(const Runner& r) {
    std::string cat;
    for (std::size_t i = 0; i < r.cases.size(); ++i) {
        if (r.cases[i]->category() != cat) {
            cat = r.cases[i]->category();
            std::cout << "\n  -- " << cat << " --\n";
        }
        std::cout << "  [" << std::setw(2) << (i + 1) << "] " << r.cases[i]->name() << "  " << r.cases[i]->description() << "\n";
    }
}

const char* mark(const std::string& status) {
    if (status == "ok") return "[OK] ";
    if (status == "fail") return "[LOI]";
    return "  .  ";
}

void printDetail(const TestReport& rep) {
    std::cout << "\n==== " << rep.name << " — " << rep.description << "\n";

    std::cout << "\n  Luong thu giua cac chain:\n";
    if (rep.flow.empty()) std::cout << "    (khong co buoc nao)\n";
    for (const auto& f : rep.flow) {
        std::cout << "  " << mark(f.status) << " " << std::setw(2) << f.index << ". " << shorten(f.from, 16)
                  << " --[" << f.label << "]--> " << shorten(f.to, 16);
        if (!f.detail.empty() && f.status == "fail") std::cout << "\n             ly do: " << f.detail;
        std::cout << "\n";
    }

    std::cout << "\n  Thay doi trang thai (State Changes):\n";
    if (rep.changes.empty()) std::cout << "    (khong doi)\n";
    for (const auto& c : rep.changes)
        std::cout << "    " << std::left << std::setw(58) << c.key << std::right << " "
                  << (c.before.empty() ? "—" : c.before) << "  ->  " << (c.after.empty() ? "—" : c.after) << "\n";

    std::cout << "\n  Kiem tra (Assert):\n";
    for (const auto& a : rep.assertions)
        std::cout << "    " << (a.passed ? "dat  " : "SAI  ") << a.description << " | mong doi: " << a.expected
                  << " | thuc te: " << a.actual << "\n";
    if (!rep.error.empty()) std::cout << "    LOI: " << rep.error << "\n";
    std::cout << "\n  Ket qua: " << (rep.passed ? "DAT" : "KHONG DAT") << (rep.reverted ? "  (da RevertState)" : "") << "\n";
}

std::vector<std::size_t> parseSelection(const std::string& s, std::size_t n) {
    std::vector<std::size_t> out;
    if (s == "all" || s == "a") {
        for (std::size_t i = 0; i < n; ++i) out.push_back(i);
        return out;
    }
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ',')) {
        try {
            std::size_t k = std::stoul(part);
            if (k >= 1 && k <= n) out.push_back(k - 1);
        } catch (const std::exception&) {}
    }
    return out;
}

void exportJson(const Runner& r, const std::string& path) {
    ReportExporter::writeJson(path, r.sb, r.lastReports);
    std::string js = path.size() > 5 && path.substr(path.size() - 5) == ".json" ? path.substr(0, path.size() - 5) + ".js" : path + ".js";
    ReportExporter::writeJs(js, r.sb, r.lastReports);
    std::cout << "Da xuat " << path << " va " << js << "\n";
}

void saveDb(const Runner& r, const std::string& db, const std::string& schema) {
#ifdef CC_HAVE_SQLITE
    ReportRepository repo(db, schema);
    std::cout << "Da ghi " << repo.saveRun(r.sb, r.lastReports) << " lan chay vao " << db << "\n";
#else
    (void)r; (void)schema;
    std::cout << "Ban build nay khong co SQLite3 — bo qua ghi " << db << "\n";
#endif
}

int interactive(Runner& r) {
    std::cout << "=========== CROSS-CHAIN TEST RUNNER — Nhom 10 ===========\n";
    for (;;) {
        printList(r);
        std::cout << "\n  So kich ban (vd 1 hoac 1,3) | a = chay tat ca | e = xuat report.json | d = ghi crosschain.db | q = thoat\n> ";
        std::string line;
        if (!std::getline(std::cin, line) || line == "q") return 0;
        if (line == "e") { exportJson(r, "report.json"); continue; }
        if (line == "d") { saveDb(r, "crosschain.db", "data/schema.sql"); continue; }
        auto sel = parseSelection(line, r.cases.size());
        if (sel.empty()) continue;
        auto reps = r.run(sel);
        if (reps.size() == 1) printDetail(reps[0]);
        else TestSuite::printReport(std::cout, reps);
    }
}

} // namespace

int main(int argc, char** argv) {
    Runner r;
    if (argc == 1) return interactive(r);

    std::string runSel, jsonPath, dbPath, schema = "data/schema.sql";
    bool list = false, quiet = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--list") list = true;
        else if (a == "--run") runSel = next();
        else if (a == "--json") jsonPath = next();
        else if (a == "--db") dbPath = next();
        else if (a == "--schema") schema = next();
        else if (a == "-q") quiet = true;
    }
    if (list) { printList(r); std::cout << "\n"; }
    if (runSel.empty() && (!jsonPath.empty() || !dbPath.empty())) runSel = "all";
    if (runSel.empty()) return 0;

    auto reps = r.run(parseSelection(runSel, r.cases.size()));
    if (!quiet && reps.size() <= 3)
        for (const auto& rep : reps) printDetail(rep);
    std::cout << "\n";
    TestSuite::printReport(std::cout, reps);
    if (!jsonPath.empty()) exportJson(r, jsonPath);
    if (!dbPath.empty()) saveDb(r, dbPath, schema);

    // Mã thoát 0 nếu mọi kịch bản (trừ kịch bản cố ý sai TH8) đều ĐẠT.
    for (const auto& rep : reps)
        if (!rep.passed && rep.name != "TH8_co_y_sai") return 1;
    return 0;
}
