#pragma once
// =====================================================================
//  ReportExporter — xuất kết quả chạy kịch bản cho giao diện:
//    report.json : dữ liệu thuần (dùng cho công cụ khác)
//    report.js   : "window.CC_REPORT = {...};" để dashboard/index.html mở
//                  thẳng bằng trình duyệt (file://) mà không cần máy chủ.
// =====================================================================
#include "cc/testing/TestCase.h"

#include <string>
#include <vector>

namespace cc {

class ReportExporter {
public:
    static std::string toJson(const Sandbox& sb, const std::vector<TestReport>& reports);
    static void writeJson(const std::string& path, const Sandbox& sb, const std::vector<TestReport>& reports);
    static void writeJs(const std::string& path, const Sandbox& sb, const std::vector<TestReport>& reports);

    static std::string escape(const std::string& s);   // chuỗi -> chuỗi JSON (có dấu ngoặc kép)
};

} // namespace cc
