#pragma once
// =====================================================================
//  CrossChainRelayer — "người đưa thư tự động" của Bước 3.
//
//  1. NGHE   : đăng ký listenEvent("Locked") trên Source Chain Adapter
//  2. DỰNG   : mỗi sự kiện -> một CrossChainMessage + con dấu (proof)
//  3. KIỂM   : CrossChainBridge::verify() — Proof Validation giả lập
//  4. GỬI    : CrossChainBridge::relay() -> giao dịch trên Target Chain Adapter
//  5. THỬ LẠI: chain đích lỗi -> thử lại tối đa maxAttempts lần
//  6. HOÀN   : vẫn lỗi -> gửi "unlock" trả tiền cho người gửi ở chain nguồn
//
//  Relayer dùng Bridge (Abstraction) chứ không gọi thẳng adapter đích, nên
//  đổi chain đích không cần sửa Relayer.
//
//  Sự kiện chỉ được XẾP HÀNG khi tới; processPending() mới xử lý — giống
//  một vòng lặp chạy định kỳ của relayer thật, và giúp test chạy tuần tự.
// =====================================================================
#include "cc/adapter/IBlockchainAdapter.h"
#include "cc/bridge/CrossChainBridge.h"

#include <deque>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace cc {

enum class RelayStatus {
    PENDING,    // vừa nhận sự kiện, chưa xử lý
    RETRYING,   // chain đích lỗi, sẽ thử lại ở vòng sau
    EXECUTED,   // chain đích đã thực thi thành công
    REJECTED,   // con dấu không hợp lệ -> dừng hẳn, cần người kiểm tra
    FAILED,     // hết lượt thử, chưa hoàn được tiền
    REFUNDED    // hết lượt thử, đã trả tiền về người gửi
};

const char* toString(RelayStatus s);

// Bản ghi một lần chuyển thư — dữ liệu cho Dashboard ở Bước 5.
struct RelayRecord {
    std::size_t              id = 0;
    Event                    sourceEvent;
    CrossChainMessage        message;
    RelayStatus              status = RelayStatus::PENDING;
    int                      attempts = 0;
    std::string              lastError;
    Hash                     targetTxHash;
    Hash                     refundTxHash;
    std::vector<std::string> log;          // nhật ký từng bước
};

struct RelayerConfig {
    int         maxAttempts = 3;          // số lần thử gửi sang chain đích
    bool        refundOnFailure = true;   // hết lượt thì hoàn tiền
    Address     sourceOperator;           // tài khoản gửi lệnh "unlock" khi hoàn tiền
    std::string eventName = "Locked";     // sự kiện cần nghe ở chain nguồn
};

// OBSERVER: được gọi ở mỗi bước để bên ngoài (Sandbox, Dashboard) ghi nhật ký luồng thư.
// stage: "event" | "verify" | "execute" | "retry" | "refund"
using RelayObserver = std::function<void(const RelayRecord& record, const std::string& stage,
                                         bool ok, const std::string& detail)>;

class CrossChainRelayer {
public:
    CrossChainRelayer(std::shared_ptr<CrossChainBridge> bridge,
                      std::shared_ptr<IBlockchainAdapter> source,
                      RelayerConfig config = {});

    // Đăng ký nghe "Locked" trên chain nguồn (gọi nhiều lần cũng chỉ đăng ký một lần).
    // Lưu ý: relayer phải sống lâu hơn adapter nguồn vì adapter giữ callback.
    void start();

    // Nhận một sự kiện "Locked". Trả false nếu sự kiện này đã nhận rồi (chống trùng).
    bool onLockedEvent(const Event& e);

    // Một vòng xử lý: mỗi thư đang chờ được thử đúng một lần. Trả số thư đã xử lý.
    std::size_t processPending();

    // Chạy các vòng cho tới khi hết thư chờ (hoặc tới maxRounds).
    std::size_t runUntilIdle(int maxRounds = 10);

    void setObserver(RelayObserver obs) { observer_ = std::move(obs); }
    const RelayerConfig& config() const { return config_; }
    const CrossChainBridge& bridge() const { return *bridge_; }

    const std::vector<RelayRecord>& records() const { return records_; }
    std::size_t pendingCount() const { return queue_.size(); }
    std::size_t count(RelayStatus s) const;

    // Memento: bản ghi, hàng chờ, sự kiện đã thấy (Sandbox chụp / khôi phục).
    struct State {
        std::vector<RelayRecord> records;
        std::deque<std::size_t>  queue;
        std::set<std::string>    seenEvents;
        std::uint64_t            localSeq = 0;
    };
    State saveState() const { return {records_, queue_, seenEvents_, localSeq_}; }
    void restoreState(const State& s) {
        records_ = s.records; queue_ = s.queue; seenEvents_ = s.seenEvents; localSeq_ = s.localSeq;
    }

private:
    CrossChainMessage buildMessage(const Event& e);
    void attempt(RelayRecord& r);
    void refund(RelayRecord& r);
    void notify(const RelayRecord& r, const std::string& stage, bool ok, const std::string& detail) const {
        if (observer_) observer_(r, stage, ok, detail);
    }

    RelayObserver                       observer_;
    std::shared_ptr<CrossChainBridge>   bridge_;
    std::shared_ptr<IBlockchainAdapter> source_;
    RelayerConfig                       config_;
    std::vector<RelayRecord>            records_;
    std::deque<std::size_t>             queue_;
    std::set<std::string>               seenEvents_;
    std::uint64_t                       localSeq_ = 0;
    bool                                started_ = false;
};

} // namespace cc
