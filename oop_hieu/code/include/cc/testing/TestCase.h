#pragma once
// =====================================================================
//  TestCase — TEMPLATE METHOD PATTERN
//  run() cố định thứ tự; lớp con chỉ viết arrange(), act(), assertResult().
//
//     ① snapshot (trước Arrange)        -> điểm quay về
//     ② arrange()                       -> thiết lập trạng thái đầu vào
//        snapshot view (trước Act)      -> mốc để so thay đổi
//     ③ act()                           -> thực thi giao dịch xuyên chuỗi
//     ④ assertResult()                  -> kiểm tra trạng thái cuối
//     ⑤ revertState() nếu KHÔNG ĐẠT     -> (hoặc luôn luôn, nếu alwaysRevert)
//     ⑥ TestReport
//
//  TestSuite — chạy nhiều TestCase trên cùng một Sandbox, in bảng tổng kết.
// =====================================================================
#include "cc/testing/Sandbox.h"

#include <functional>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace cc {

struct TestReport {
    std::string                  name;
    std::string                  description;
    std::string                  category;           // "Token Bridge", "Lending", "Kiểm tra RevertState"...
    bool                         passed = false;
    bool                         reverted = false;   // đã gọi revertState() sau test
    std::string                  error;              // ngoại lệ nếu test bị lỗi giữa chừng
    std::vector<AssertionResult> assertions;
    std::vector<StateChange>     changes;            // thay đổi do bước Act gây ra
    std::vector<FlowStep>        flow;               // luồng thư trong bước Act (vẽ sơ đồ tuần tự)
    std::vector<RouteRelay>      relays;             // các thư relayer đã xử lý (chụp trước khi revert)
};

class TestCase {
public:
    TestCase(std::string name, std::string description, std::string category = "Token Bridge")
        : name_(std::move(name)), description_(std::move(description)), category_(std::move(category)) {}
    virtual ~TestCase() = default;

    // alwaysRevert = true : luôn quay về sau test (các test độc lập với nhau)
    // alwaysRevert = false: chỉ quay về khi test KHÔNG ĐẠT (đúng câu chữ đề tài)
    TestReport run(Sandbox& sb, bool alwaysRevert = true);

    const std::string& name() const { return name_; }
    const std::string& description() const { return description_; }
    const std::string& category() const { return category_; }

protected:
    virtual void arrange(Sandbox& sb) = 0;
    virtual void act(Sandbox& sb) = 0;
    virtual void assertResult(Sandbox& sb) = 0;

private:
    std::string name_;
    std::string description_;
    std::string category_;
};

// Viết nhanh một test bằng 3 hàm lambda (không cần tạo lớp con).
class LambdaTestCase : public TestCase {
public:
    using Step = std::function<void(Sandbox&)>;
    LambdaTestCase(std::string name, std::string desc, Step arrange, Step act, Step check,
                   std::string category = "Kiểm tra RevertState")
        : TestCase(std::move(name), std::move(desc), std::move(category)),
          arrange_(std::move(arrange)), act_(std::move(act)), check_(std::move(check)) {}

protected:
    void arrange(Sandbox& sb) override { if (arrange_) arrange_(sb); }
    void act(Sandbox& sb) override { if (act_) act_(sb); }
    void assertResult(Sandbox& sb) override { if (check_) check_(sb); }

private:
    Step arrange_, act_, check_;
};

class TestSuite {
public:
    explicit TestSuite(Sandbox& sb, bool alwaysRevert = true) : sb_(sb), alwaysRevert_(alwaysRevert) {}

    void add(std::unique_ptr<TestCase> tc) { cases_.push_back(std::move(tc)); }
    std::vector<TestReport> runAll();
    const std::vector<std::unique_ptr<TestCase>>& cases() const { return cases_; }

    // In bảng tổng kết; verbose = in cả từng phép so và thay đổi trạng thái.
    static void printReport(std::ostream& os, const std::vector<TestReport>& reports, bool verbose = false);

private:
    Sandbox&                               sb_;
    bool                                   alwaysRevert_;
    std::vector<std::unique_ptr<TestCase>> cases_;
};

} // namespace cc
