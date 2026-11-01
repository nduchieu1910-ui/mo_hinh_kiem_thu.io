#include "cc/testing/TestCase.h"

#include <exception>
#include <ostream>

namespace cc {

TestReport TestCase::run(Sandbox& sb, bool alwaysRevert) {
    TestReport rep;
    rep.name = name_;
    rep.description = description_;
    rep.category = category_;

    sb.clearAssertions();
    const SandboxSnapshot before = sb.snapshot("before-" + name_);   // ① điểm quay về
    StateView beforeAct = before.view;

    try {
        arrange(sb);                                                  // ②
        beforeAct = sb.stateView();                                   // mốc so thay đổi của Act
        sb.clearFlow();                                               // sơ đồ luồng chỉ ghi bước Act
        act(sb);                                                      // ③
        assertResult(sb);                                             // ④
    } catch (const std::exception& e) {
        rep.error = e.what();
    }

    rep.assertions = sb.assertions();
    rep.flow = sb.flow();
    rep.relays = sb.allRelayRecords();
    rep.changes = diffState(beforeAct, sb.stateView());
    rep.passed = rep.error.empty() && !rep.assertions.empty() && sb.allPassed();

    if (!rep.passed || alwaysRevert) {                                // ⑤ RevertState
        sb.revertState(before);
        rep.reverted = true;
    }
    sb.clearAssertions();
    sb.clearFlow();
    return rep;                                                       // ⑥
}

std::vector<TestReport> TestSuite::runAll() {
    std::vector<TestReport> out;
    for (auto& tc : cases_) out.push_back(tc->run(sb_, alwaysRevert_));
    return out;
}

void TestSuite::printReport(std::ostream& os, const std::vector<TestReport>& reports, bool verbose) {
    std::size_t passed = 0;
    for (const auto& r : reports) {
        passed += r.passed;
        os << (r.passed ? "[DAT]       " : "[KHONG DAT] ") << r.name;
        if (r.reverted) os << "  (da revertState)";
        os << "\n";
        if (!r.error.empty()) os << "      loi: " << r.error << "\n";
        for (const auto& a : r.assertions)
            if (verbose || !a.passed)
                os << "      " << (a.passed ? "ok  " : "SAI ") << a.description
                   << " | mong doi: " << a.expected << " | thuc te: " << a.actual << "\n";
        if (verbose)
            for (const auto& c : r.changes)
                os << "      ~ " << c.key << ": " << (c.before.empty() ? "(chua co)" : c.before)
                   << " -> " << (c.after.empty() ? "(mat)" : c.after) << "\n";
    }
    os << "Tong: " << passed << "/" << reports.size() << " kich ban DAT\n";
}

} // namespace cc
