#pragma once
// =====================================================================
//  Sandbox — "phòng thí nghiệm" của Bước 4.
//
//  Gom cả thế giới mô phỏng vào một chỗ: nhiều chain (sổ cái + adapter),
//  các tuyến xuyên chuỗi (validator + bridge + relayer), và cung cấp:
//
//    ARRANGE : setBalance, giveUtxo, setStorage, failNext, setActiveSigners
//    ACT     : lock, relayAll, send, submitRaw, injectMessage
//    ASSERT  : expectBalance, expectRelayStatus, expectStorage, expectTxStatus...
//              (so sai KHÔNG dừng chương trình, chỉ ghi vào danh sách)
//    STATE   : snapshot()  -> SandboxSnapshot  (Memento của mọi thành phần)
//              revertState(snapshot)           (trả mọi thứ về lúc chụp)
// =====================================================================
#include "cc/adapter/EVMAdapter.h"
#include "cc/adapter/MockChainAdapter.h"
#include "cc/adapter/SolanaAdapter.h"
#include "cc/adapter/UtxoAdapter.h"
#include "cc/bridge/LendingBridge.h"
#include "cc/bridge/MultiSigProofValidator.h"
#include "cc/ledger/LocalLedgerSimulator.h"
#include "cc/relayer/CrossChainRelayer.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace cc {

// Một phép so trong phần Assert (khớp bảng assertion_results của database).
struct AssertionResult {
    std::string description;
    std::string expected;
    std::string actual;
    bool        passed = false;
};

// Một dòng thay đổi trạng thái (khớp bảng state_changes của database).
struct StateChange {
    std::string key;      // vd: "ChainA-EVM/account/0xa11ce/balance"
    std::string before;   // rỗng = chưa tồn tại
    std::string after;    // rỗng = đã biến mất
};

// Một bước trong luồng luân chuyển thư (để vẽ sơ đồ tuần tự trên Dashboard).
struct FlowStep {
    std::size_t index = 0;
    std::string from;      // người / chain / thành phần gửi
    std::string to;        // người / chain / thành phần nhận
    std::string label;     // nội dung: "lock 100", "Locked #0", "kiểm chữ ký thư #0"...
    std::string status;    // "ok" | "fail" | "info"
    std::string detail;    // lý do lỗi, ghi chú
};

// Một thư trên một tuyến (dùng cho báo cáo / bảng cross_chain_messages).
struct RouteRelay {
    std::string src;
    std::string dst;
    RelayRecord record;
};

// Loại nghiệp vụ của một tuyến (phía Abstraction của Bridge pattern).
enum class BridgeKind { Token, Lending, Unlock };
const char* toString(BridgeKind k);

struct RouteOptions {
    BridgeKind           kind = BridgeKind::Token;
    std::string          eventName = "Locked";              // sự kiện relayer nghe ở chain nguồn
    std::vector<Address> validators{"val_a", "val_b", "val_c"};
    std::size_t          threshold = 2;
    int                  maxAttempts = 3;
    bool                 refundOnFailure = true;
};

// Trạng thái phẳng: khóa -> giá trị, dùng để so trước / sau.
using StateView = std::map<std::string, std::string>;
std::vector<StateChange> diffState(const StateView& before, const StateView& after);

// Ảnh chụp toàn bộ Sandbox (Memento tổng hợp).
struct SandboxSnapshot {
    std::string                                       label;
    std::map<std::string, LedgerSnapshot>             ledgers;      // theo tên chain
    std::map<std::string, std::set<Hash>>             engines;      // giao dịch Solana đã xử lý
    std::map<std::string, MockChainAdapter::State>    mocks;
    std::map<std::string, MultiSigProofValidator::State> validators; // theo tuyến "A->B"
    std::map<std::string, CrossChainRelayer::State>   relayers;
    StateView                                         view;         // để so thay đổi
};

class Sandbox {
public:
    Sandbox() = default;
    Sandbox(const Sandbox&) = delete;             // relayer giữ callback trỏ vào chính nó
    Sandbox& operator=(const Sandbox&) = delete;

    // ----- Dựng thế giới -------------------------------------------
    void addEvmChain(const std::string& name);
    void addUtxoChain(const std::string& name);
    void addSolanaChain(const std::string& name);
    void addMockChain(const std::string& name);

    // Nối tuyến src -> dst: tạo MultiSigProofValidator + Bridge (theo kind) + CrossChainRelayer.
    void connect(const std::string& src, const std::string& dst, RouteOptions options = {});

    // ----- ARRANGE -------------------------------------------------
    void setBalance(const std::string& chain, const Address& who, Amount amount);
    std::string giveUtxo(const std::string& chain, const Address& owner, Amount amount);  // trả id tờ tiền
    void setStorage(const std::string& chain, const Address& contract, const std::string& slot, const std::string& value);
    void failNext(const std::string& mockChain, TxStatus status, const std::string& reason, int count = 1);
    void setActiveSigners(const std::string& src, const std::string& dst, std::vector<Address> signers);

    // ----- ACT -----------------------------------------------------
    // Người dùng khóa tiền ở src để chuyển sang dst (relayer src->dst sẽ nhận sự kiện).
    // memo: ghi chú đi kèm thư (vd "borrow=400" cho Lending).
    TransactionResult lock(const std::string& src, const std::string& dst,
                           const Address& from, Amount amount, const Address& receiver,
                           const std::string& memo = "");
    std::size_t relayAll();                                         // mọi relayer chạy tới hết thư chờ
    TransactionResult send(const std::string& chain, const TxPayload& payload);
    TransactionResult submitRaw(const std::string& chain, const ITransaction& tx);
    TransactionResult injectMessage(const std::string& src, const std::string& dst, CrossChainMessage msg);

    // Dựng một lá thư hợp lệ (có con dấu của validator tuyến) — dùng để thử thư giả / sửa thư.
    CrossChainMessage makeMessage(const std::string& src, const std::string& dst, const Address& sender,
                                  const Address& receiver, Amount amount, std::uint64_t nonce) const;

    // ----- Đọc trạng thái -----------------------------------------
    Amount balance(const std::string& chain, const Address& who) const;
    const std::vector<RelayRecord>& relayRecords(const std::string& src, const std::string& dst) const;
    std::vector<RouteRelay> allRelayRecords() const;                  // thư của mọi tuyến
    std::shared_ptr<IBlockchainAdapter> adapter(const std::string& chain) const;
    std::shared_ptr<LocalLedgerSimulator> ledger(const std::string& chain) const;   // null với Mock
    StateView stateView() const;

    // ----- ASSERT --------------------------------------------------
    void expectBalance(const std::string& chain, const Address& who, Amount expected);
    void expectRelayCount(const std::string& src, const std::string& dst, std::size_t expected);
    void expectRelayStatus(const std::string& src, const std::string& dst, std::size_t index, RelayStatus expected);
    void expectStorage(const std::string& chain, const Address& contract, const std::string& slot, const std::string& expected);
    void expectTxStatus(const TransactionResult& r, TxStatus expected, const std::string& what);
    void expectTrue(bool condition, const std::string& description);

    const std::vector<AssertionResult>& assertions() const { return assertions_; }
    void clearAssertions() { assertions_.clear(); }
    bool allPassed() const;

    // ----- SNAPSHOT & REVERTSTATE ---------------------------------
    SandboxSnapshot snapshot(const std::string& label = "") const;
    void revertState(const SandboxSnapshot& snap);

    std::vector<std::string> chainNames() const;
    std::vector<std::pair<std::string, std::string>> routes() const;
    ChainType chainType(const std::string& chain) const { return this->chain(chain).type; }
    BridgeKind routeKind(const std::string& src, const std::string& dst) const { return route(src, dst).kind; }

    // ----- FLOW TRACE (Observer) — nhật ký luồng thư cho Dashboard ----
    const std::vector<FlowStep>& flow() const { return trace_; }
    void clearFlow() { trace_.clear(); }
    void recordFlow(const std::string& from, const std::string& to, const std::string& label,
                    const std::string& status, const std::string& detail = "");

private:
    struct Chain {
        ChainType                             type;
        std::shared_ptr<LocalLedgerSimulator> ledger;       // null với Mock
        std::shared_ptr<LedgerBackedAdapter>  ledgerAdapter;
        std::shared_ptr<MockChainAdapter>     mock;
        std::shared_ptr<IBlockchainAdapter>   adapter;
    };
    struct Route {
        BridgeKind                              kind = BridgeKind::Token;
        std::shared_ptr<MultiSigProofValidator> validator;
        std::shared_ptr<CrossChainBridge>       bridge;
        std::unique_ptr<CrossChainRelayer>      relayer;
    };

    void onChainEvent(const std::string& chain, const Event& e);
    void onRelayStep(const std::string& src, const std::string& dst, const RelayRecord& r,
                     const std::string& stage, bool ok, const std::string& detail);
    std::vector<FlowStep>          trace_;

    void addLedgerChain(const std::string& name, ChainType type);
    Chain& chain(const std::string& name);
    const Chain& chain(const std::string& name) const;
    Route& route(const std::string& src, const std::string& dst);
    const Route& route(const std::string& src, const std::string& dst) const;
    static std::string routeKey(const std::string& src, const std::string& dst) { return src + "->" + dst; }
    void record(const std::string& description, const std::string& expected, const std::string& actual);

    std::map<std::string, Chain>   chains_;
    std::map<std::string, Route>   routes_;
    std::vector<AssertionResult>   assertions_;
    std::uint64_t                  utxoSeq_ = 0;
};

} // namespace cc
