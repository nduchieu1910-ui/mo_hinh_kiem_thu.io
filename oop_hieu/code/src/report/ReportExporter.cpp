#include "cc/report/ReportExporter.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace cc {

std::string ReportExporter::escape(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);   // UTF-8 giữ nguyên
                }
        }
    }
    return out + "\"";
}

namespace {

const char* chainTypeName(ChainType t) { return toString(t); }

std::string str(const std::string& s) { return ReportExporter::escape(s); }
const char* boolean(bool b) { return b ? "true" : "false"; }

} // namespace

std::string ReportExporter::toJson(const Sandbox& sb, const std::vector<TestReport>& reports) {
    std::ostringstream o;
    std::size_t passed = 0;
    for (const auto& r : reports) passed += r.passed;

    o << "{\n  \"project\": " << str("Nhóm 10 — Khung kiểm thử hợp đồng thông minh xuyên chuỗi") << ",\n";
    o << "  \"summary\": {\"total\": " << reports.size() << ", \"passed\": " << passed
      << ", \"failed\": " << (reports.size() - passed) << "},\n";

    // ----- Thế giới mô phỏng: chain + tuyến -----
    o << "  \"world\": {\n    \"chains\": [";
    bool first = true;
    for (const auto& name : sb.chainNames()) {
        o << (first ? "" : ", ") << "{\"name\": " << str(name) << ", \"type\": " << str(chainTypeName(sb.chainType(name))) << "}";
        first = false;
    }
    o << "],\n    \"routes\": [";
    first = true;
    for (const auto& [src, dst] : sb.routes()) {
        o << (first ? "" : ", ") << "{\"src\": " << str(src) << ", \"dst\": " << str(dst)
          << ", \"kind\": " << str(toString(sb.routeKind(src, dst))) << "}";
        first = false;
    }
    o << "]\n  },\n";

    // ----- Từng kịch bản -----
    o << "  \"reports\": [\n";
    for (std::size_t i = 0; i < reports.size(); ++i) {
        const TestReport& r = reports[i];
        o << "    {\n";
        o << "      \"name\": " << str(r.name) << ",\n";
        o << "      \"description\": " << str(r.description) << ",\n";
        o << "      \"category\": " << str(r.category) << ",\n";
        o << "      \"passed\": " << boolean(r.passed) << ", \"reverted\": " << boolean(r.reverted)
          << ", \"error\": " << str(r.error) << ",\n";

        o << "      \"assertions\": [";
        for (std::size_t k = 0; k < r.assertions.size(); ++k) {
            const auto& a = r.assertions[k];
            o << (k ? ", " : "") << "\n        {\"description\": " << str(a.description) << ", \"expected\": " << str(a.expected)
              << ", \"actual\": " << str(a.actual) << ", \"passed\": " << boolean(a.passed) << "}";
        }
        o << "],\n      \"changes\": [";
        for (std::size_t k = 0; k < r.changes.size(); ++k) {
            const auto& c = r.changes[k];
            o << (k ? ", " : "") << "\n        {\"key\": " << str(c.key) << ", \"before\": " << str(c.before)
              << ", \"after\": " << str(c.after) << "}";
        }
        o << "],\n      \"flow\": [";
        for (std::size_t k = 0; k < r.flow.size(); ++k) {
            const auto& f = r.flow[k];
            o << (k ? ", " : "") << "\n        {\"index\": " << f.index << ", \"from\": " << str(f.from) << ", \"to\": " << str(f.to)
              << ", \"label\": " << str(f.label) << ", \"status\": " << str(f.status) << ", \"detail\": " << str(f.detail) << "}";
        }
        o << "],\n      \"messages\": [";
        for (std::size_t k = 0; k < r.relays.size(); ++k) {
            const auto& rr = r.relays[k];
            const auto& m = rr.record.message;
            o << (k ? ", " : "") << "\n        {\"src\": " << str(rr.src) << ", \"dst\": " << str(rr.dst)
              << ", \"nonce\": " << m.nonce << ", \"sender\": " << str(m.sender) << ", \"receiver\": " << str(m.receiver)
              << ", \"amount\": " << str(amountToString(m.amount)) << ", \"hash\": " << str(m.messageHash)
              << ", \"status\": " << str(toString(rr.record.status)) << ", \"attempts\": " << rr.record.attempts
              << ", \"error\": " << str(rr.record.lastError) << ", \"log\": [";
            for (std::size_t j = 0; j < rr.record.log.size(); ++j) o << (j ? ", " : "") << str(rr.record.log[j]);
            o << "]}";
        }
        o << "]\n    }" << (i + 1 < reports.size() ? "," : "") << "\n";
    }
    o << "  ]\n}\n";
    return o.str();
}

void ReportExporter::writeJson(const std::string& path, const Sandbox& sb, const std::vector<TestReport>& reports) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Không ghi được file " + path);
    f << toJson(sb, reports);
}

void ReportExporter::writeJs(const std::string& path, const Sandbox& sb, const std::vector<TestReport>& reports) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Không ghi được file " + path);
    f << "// Tạo bởi test_runner — dashboard/index.html đọc biến này\nwindow.CC_REPORT = " << toJson(sb, reports) << ";\n";
}

} // namespace cc
