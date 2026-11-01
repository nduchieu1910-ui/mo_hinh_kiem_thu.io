#include "cc/report/ReportRepository.h"

#include "cc/report/ReportExporter.h"

#include <sqlite3.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace cc {

namespace {

// Bọc sqlite3_stmt cho gọn (tự finalize).
class Stmt {
public:
    Stmt(sqlite3* db, const std::string& sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st_, nullptr) != SQLITE_OK)
            throw std::runtime_error(std::string("SQLite prepare: ") + sqlite3_errmsg(db) + " | " + sql);
    }
    ~Stmt() { sqlite3_finalize(st_); }
    Stmt& bind(int i, const std::string& v) { sqlite3_bind_text(st_, i, v.c_str(), -1, SQLITE_TRANSIENT); return *this; }
    Stmt& bind(int i, long long v) { sqlite3_bind_int64(st_, i, v); return *this; }
    Stmt& bindNull(int i) { sqlite3_bind_null(st_, i); return *this; }
    bool step() {
        int rc = sqlite3_step(st_);
        if (rc == SQLITE_ROW) return true;
        if (rc == SQLITE_DONE) return false;
        throw std::runtime_error(std::string("SQLite step: ") + sqlite3_errmsg(db_));
    }
    long long colInt(int i) { return sqlite3_column_int64(st_, i); }
private:
    sqlite3*      db_;
    sqlite3_stmt* st_ = nullptr;
};

struct ChainMeta { const char* type; const char* ledger; const char* adapter; const char* symbol; int decimals; };

ChainMeta meta(ChainType t) {
    switch (t) {
        case ChainType::EVM:    return {"EVM", "ACCOUNT", "EVMAdapter", "ETH", 18};
        case ChainType::UTXO:   return {"UTXO", "UTXO", "UtxoAdapter", "BTC", 8};
        case ChainType::SOLANA: return {"SOLANA", "ACCOUNT", "SolanaAdapter", "SOL", 9};
        case ChainType::MOCK:   break;
    }
    return {"MOCK", "ACCOUNT", "MockChainAdapter", "MOCK", 0};
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, sep)) out.push_back(part);
    return out;
}

} // namespace

ReportRepository::ReportRepository(const std::string& dbPath, const std::string& schemaSqlPath) {
    if (sqlite3_open(dbPath.c_str(), &db_) != SQLITE_OK) {
        std::string err = db_ ? sqlite3_errmsg(db_) : "không mở được";
        sqlite3_close(db_);
        throw std::runtime_error("Không mở được database " + dbPath + ": " + err);
    }
    exec("PRAGMA foreign_keys = ON;");

    Stmt check(db_, "SELECT count(*) FROM sqlite_master WHERE type='table' AND name='chains'");
    check.step();
    if (check.colInt(0) == 0) {
        std::ifstream f(schemaSqlPath, std::ios::binary);
        if (!f) throw std::runtime_error("Database trống và không đọc được schema: " + schemaSqlPath);
        std::stringstream buf;
        buf << f.rdbuf();
        exec(buf.str());
    }
}

ReportRepository::~ReportRepository() { sqlite3_close(db_); }

void ReportRepository::exec(const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "?";
        sqlite3_free(err);
        throw std::runtime_error("SQLite: " + msg);
    }
}

long long ReportRepository::chainId(const std::string& name) {
    Stmt q(db_, "SELECT chain_id FROM chains WHERE name = ?");
    q.bind(1, name);
    return q.step() ? q.colInt(0) : -1;
}

long long ReportRepository::ensureTestCase(const TestReport& r, const Sandbox& sb) {
    {
        Stmt q(db_, "SELECT test_case_id FROM test_cases WHERE name = ?");
        q.bind(1, r.name);
        if (q.step()) return q.colInt(0);
    }
    // Chain nguồn / đích = hai chain đầu tiên xuất hiện trong luồng thư.
    std::vector<std::string> seen;
    auto names = sb.chainNames();
    for (const auto& f : r.flow)
        for (const auto* p : {&f.from, &f.to})
            for (const auto& n : names)
                if (*p == n && std::find(seen.begin(), seen.end(), n) == seen.end()) seen.push_back(n);
    for (const auto& n : names)
        if (seen.size() < 2 && std::find(seen.begin(), seen.end(), n) == seen.end()) seen.push_back(n);

    std::string scenario = r.category == "Lending" ? "LENDING" : (r.category == "Token Bridge" ? "TOKEN_BRIDGE" : "CUSTOM");
    std::string expected = "[";
    for (std::size_t i = 0; i < r.assertions.size(); ++i)
        expected += (i ? "," : "") + std::string("{\"description\":") + ReportExporter::escape(r.assertions[i].description) +
                    ",\"expected\":" + ReportExporter::escape(r.assertions[i].expected) + "}";
    expected += "]";
    std::string act = "[";
    for (std::size_t i = 0; i < r.flow.size(); ++i) act += (i ? "," : "") + ReportExporter::escape(r.flow[i].label);
    act += "]";

    Stmt ins(db_, "INSERT INTO test_cases (name, description, scenario_type, source_chain_id, target_chain_id, "
                  "arrange_json, act_json, expected_json) VALUES (?, ?, ?, ?, ?, '{}', ?, ?)");
    ins.bind(1, r.name).bind(2, r.description).bind(3, scenario)
       .bind(4, chainId(seen.at(0))).bind(5, chainId(seen.at(1))).bind(6, act).bind(7, expected);
    ins.step();
    return sqlite3_last_insert_rowid(db_);
}

std::size_t ReportRepository::saveRun(const Sandbox& sb, const std::vector<TestReport>& reports) {
    exec("BEGIN;");
    try {
        for (const auto& name : sb.chainNames()) {
            ChainMeta m = meta(sb.chainType(name));
            Stmt ins(db_, "INSERT OR IGNORE INTO chains (name, chain_type, ledger_model, adapter_class, native_symbol, decimals) "
                          "VALUES (?, ?, ?, ?, ?, ?)");
            ins.bind(1, name).bind(2, m.type).bind(3, m.ledger).bind(4, m.adapter).bind(5, m.symbol).bind(6, m.decimals);
            ins.step();
        }

        for (const auto& r : reports) {
            const long long tc = ensureTestCase(r, sb);
            Stmt run(db_, "INSERT INTO test_runs (test_case_id, finished_at, result, error_message, rolled_back) "
                          "VALUES (?, datetime('now'), ?, ?, ?)");
            run.bind(1, tc).bind(2, std::string(!r.error.empty() ? "ERROR" : (r.passed ? "PASSED" : "FAILED")));
            if (r.error.empty()) run.bindNull(3); else run.bind(3, r.error);
            run.bind(4, static_cast<long long>(r.reverted));
            run.step();
            const long long runId = sqlite3_last_insert_rowid(db_);

            for (const auto& a : r.assertions) {
                Stmt ins(db_, "INSERT INTO assertion_results (run_id, description, expected_value, actual_value, passed) "
                              "VALUES (?, ?, ?, ?, ?)");
                ins.bind(1, runId).bind(2, a.description).bind(3, a.expected).bind(4, a.actual)
                   .bind(5, static_cast<long long>(a.passed));
                ins.step();
            }

            for (const auto& c : r.changes) {
                auto parts = split(c.key, '/');
                if (parts.size() < 3 || parts[0].find("->") != std::string::npos) continue;   // dòng của relayer
                const long long cid = chainId(parts[0]);
                if (cid < 0) continue;
                std::string type, key;
                if (parts[1] == "account" && parts.size() >= 4) { type = parts[3] == "nonce" ? "NONCE" : "BALANCE"; key = parts[2]; }
                else if (parts[1] == "storage" && parts.size() >= 4) { type = "STORAGE"; key = parts[2] + "." + parts[3]; }
                else if (parts[1] == "utxo") { type = "UTXO"; key = parts[2]; }
                else continue;
                Stmt ins(db_, "INSERT INTO state_changes (run_id, chain_id, entity_type, entity_key, old_value, new_value) "
                              "VALUES (?, ?, ?, ?, ?, ?)");
                ins.bind(1, runId).bind(2, cid).bind(3, type).bind(4, key);
                if (c.before.empty()) ins.bindNull(5); else ins.bind(5, c.before);
                if (c.after.empty()) ins.bindNull(6); else ins.bind(6, c.after);
                ins.step();
            }

            for (const auto& rr : r.relays) {
                const auto& m = rr.record.message;
                std::string status = "RELAYED";
                if (rr.record.status == RelayStatus::EXECUTED) status = "EXECUTED";
                else if (rr.record.status == RelayStatus::REJECTED || rr.record.status == RelayStatus::FAILED ||
                         rr.record.status == RelayStatus::REFUNDED) status = "FAILED";
                Stmt ins(db_, "INSERT OR REPLACE INTO cross_chain_messages (message_hash, source_chain_id, target_chain_id, "
                              "sender, receiver, nonce, payload_json, proof, status, failure_reason, executed_at) "
                              "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, datetime('now'))");
                ins.bind(1, m.messageHash).bind(2, chainId(rr.src)).bind(3, chainId(rr.dst)).bind(4, m.sender)
                   .bind(5, m.receiver).bind(6, static_cast<long long>(m.nonce))
                   .bind(7, "{\"amount\":\"" + amountToString(m.amount) + "\",\"relayStatus\":\"" + toString(rr.record.status) + "\"}")
                   .bind(8, m.proof).bind(9, status);
                if (rr.record.lastError.empty()) ins.bindNull(10); else ins.bind(10, rr.record.lastError);
                ins.step();
            }
        }
        exec("COMMIT;");
    } catch (...) {
        exec("ROLLBACK;");
        throw;
    }
    return reports.size();
}

} // namespace cc
