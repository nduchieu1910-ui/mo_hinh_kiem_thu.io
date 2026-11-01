#pragma once
// =====================================================================
//  ReportRepository — ghi kết quả chạy vào SQLite (crosschain.db) theo
//  schema đã thiết kế từ đầu (data/schema.sql):
//
//     TestReport      -> test_cases + test_runs
//     AssertionResult -> assertion_results
//     StateChange     -> state_changes
//     RelayRecord     -> cross_chain_messages
//     chain trong Sandbox -> chains
//
//  Chỉ được biên dịch khi CMake tìm thấy thư viện SQLite3.
// =====================================================================
#include "cc/testing/TestCase.h"

#include <string>
#include <vector>

struct sqlite3;

namespace cc {

class ReportRepository {
public:
    // Mở (hoặc tạo) file database. Nếu chưa có bảng, chạy schemaSqlPath để tạo.
    ReportRepository(const std::string& dbPath, const std::string& schemaSqlPath);
    ~ReportRepository();
    ReportRepository(const ReportRepository&) = delete;
    ReportRepository& operator=(const ReportRepository&) = delete;

    // Lưu một lần chạy bộ kịch bản. Trả số dòng test_runs đã ghi.
    std::size_t saveRun(const Sandbox& sb, const std::vector<TestReport>& reports);

private:
    void exec(const std::string& sql);
    long long chainId(const std::string& name);
    long long ensureTestCase(const TestReport& r, const Sandbox& sb);

    sqlite3* db_ = nullptr;
};

} // namespace cc
