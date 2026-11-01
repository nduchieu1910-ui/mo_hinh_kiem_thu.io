// =====================================================================
//  Chạy bộ 7 kịch bản chuẩn trên Sandbox và in báo cáo.
//    ./run_scenarios            bảng tổng kết
//    ./run_scenarios -v         in cả từng phép so và thay đổi trạng thái
//    ./run_scenarios --fail     thêm kịch bản cố ý sai (minh họa RevertState)
// =====================================================================
#include "cc/testing/Scenarios.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    bool verbose = false, withFailing = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-v") verbose = true;
        if (a == "--fail") withFailing = true;
    }

    cc::Sandbox sb;
    cc::scenarios::buildStandardWorld(sb);
    cc::TestSuite suite(sb);
    for (auto& tc : cc::scenarios::standardScenarios()) suite.add(std::move(tc));
    if (withFailing) suite.add(cc::scenarios::intentionallyFailingScenario());

    auto reports = suite.runAll();
    cc::TestSuite::printReport(std::cout, reports, verbose);

    for (std::size_t i = 0; i < 7 && i < reports.size(); ++i)
        if (!reports[i].passed) return 1;
    return 0;
}
