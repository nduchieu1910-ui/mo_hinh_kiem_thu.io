#include "cc/relayer/CrossChainRelayer.h"

#include <stdexcept>
#include <utility>

namespace cc {

const char* toString(RelayStatus s) {
    switch (s) {
        case RelayStatus::PENDING:  return "PENDING";
        case RelayStatus::RETRYING: return "RETRYING";
        case RelayStatus::EXECUTED: return "EXECUTED";
        case RelayStatus::REJECTED: return "REJECTED";
        case RelayStatus::FAILED:   return "FAILED";
        case RelayStatus::REFUNDED: return "REFUNDED";
    }
    return "?";
}

CrossChainRelayer::CrossChainRelayer(std::shared_ptr<CrossChainBridge> bridge,
                                     std::shared_ptr<IBlockchainAdapter> source,
                                     RelayerConfig config)
    : bridge_(std::move(bridge)), source_(std::move(source)), config_(std::move(config)) {
    if (!bridge_ || !source_) throw std::invalid_argument("CrossChainRelayer: thiếu bridge hoặc adapter nguồn");
    if (source_->chainName() != bridge_->source().chainName())
        throw std::invalid_argument("CrossChainRelayer: adapter nguồn không khớp chain nguồn của bridge");
    if (config_.maxAttempts < 1) throw std::invalid_argument("CrossChainRelayer: maxAttempts phải >= 1");
}

void CrossChainRelayer::start() {
    if (started_) return;
    started_ = true;
    source_->listenEvent(config_.eventName, [this](const Event& e) { onLockedEvent(e); });
}

bool CrossChainRelayer::onLockedEvent(const Event& e) {
    // Sự kiện ghi rõ chain đích khác -> không phải việc của relayer này
    // (một chain nguồn có thể nối nhiều chain đích, mỗi cặp một relayer).
    if (auto t = e.fields.find("targetChain"); t != e.fields.end() && !t->second.empty() &&
                                               t->second != bridge_->target().chainName())
        return false;

    // Khóa chống trùng: mã giao dịch nguồn, nếu không có thì dùng nonce.
    std::string key;
    if (auto it = e.fields.find("txHash"); it != e.fields.end()) key = "tx:" + it->second;
    else if (auto n = e.fields.find("nonce"); n != e.fields.end()) key = "nonce:" + n->second;
    if (!key.empty() && !seenEvents_.insert(key).second) return false;

    RelayRecord r;
    r.id = records_.size();
    r.sourceEvent = e;
    r.message = buildMessage(e);
    r.log.push_back("nhận sự kiện " + e.name + ", dựng thư nonce " + std::to_string(r.message.nonce));
    records_.push_back(std::move(r));
    queue_.push_back(records_.back().id);
    notify(records_.back(), "event", true, e.name);
    return true;
}

CrossChainMessage CrossChainRelayer::buildMessage(const Event& e) {
    auto field = [&](const std::string& k) {
        auto it = e.fields.find(k);
        return it == e.fields.end() ? std::string() : it->second;
    };
    CrossChainMessage m;
    m.sourceChain = bridge_->source().chainName();
    m.targetChain = bridge_->target().chainName();
    m.sender = field("sender");
    m.receiver = field("receiver");
    m.amount = field("amount").empty() ? Amount(0) : amountFromString(field("amount"));
    m.nonce = field("nonce").empty() ? localSeq_++ : std::stoull(field("nonce"));
    if (!field("txHash").empty()) m.payload["sourceTx"] = field("txHash");
    if (!field("memo").empty()) m.payload["memo"] = field("memo");      // vd: "borrow=400" cho Lending
    m.messageHash = m.computeHash();
    m.proof = bridge_->proofFor(m);   // các validator ký lên thư
    m.status = MessageStatus::SENT;
    return m;
}

std::size_t CrossChainRelayer::processPending() {
    std::deque<std::size_t> round;
    round.swap(queue_);                  // thư thử lại sẽ vào hàng của vòng sau
    for (std::size_t id : round) attempt(records_.at(id));
    return round.size();
}

std::size_t CrossChainRelayer::runUntilIdle(int maxRounds) {
    std::size_t total = 0;
    for (int i = 0; i < maxRounds && !queue_.empty(); ++i) total += processPending();
    return total;
}

void CrossChainRelayer::attempt(RelayRecord& r) {
    r.attempts += 1;
    const std::string tag = "lần " + std::to_string(r.attempts) + ": ";

    // Proof Validation: con dấu sai là dấu hiệu giả mạo -> không thử lại, không tự hoàn tiền.
    if (std::string reason = bridge_->verify(r.message); !reason.empty()) {
        r.status = RelayStatus::REJECTED;
        r.lastError = "proof: " + reason;
        r.message.status = MessageStatus::FAILED;
        r.log.push_back(tag + "TỪ CHỐI — " + reason);
        notify(r, "verify", false, reason);
        return;
    }
    notify(r, "verify", true, "đủ chữ ký");

    TransactionResult res = bridge_->relay(r.message);
    if (res.ok()) {
        r.status = RelayStatus::EXECUTED;
        r.targetTxHash = res.txHash;
        r.lastError.clear();
        r.log.push_back(tag + "chain đích thực thi thành công");
        notify(r, "execute", true, "");
        return;
    }

    r.lastError = res.revertReason;
    notify(r, "execute", false, res.revertReason);
    if (r.attempts < config_.maxAttempts) {
        r.status = RelayStatus::RETRYING;
        r.log.push_back(tag + "chain đích lỗi (" + res.revertReason + "), sẽ thử lại");
        notify(r, "retry", true, "thử lại ở vòng sau");
        queue_.push_back(r.id);
        return;
    }

    r.status = RelayStatus::FAILED;
    r.log.push_back(tag + "chain đích lỗi (" + res.revertReason + "), hết lượt thử");
    if (config_.refundOnFailure) refund(r);
}

void CrossChainRelayer::refund(RelayRecord& r) {
    TxPayload unlock{"unlock", config_.sourceOperator, r.message.sender, r.message.amount,
                     {{"messageHash", r.message.messageHash}}};
    TransactionResult res = source_->sendTransaction(unlock);
    if (res.ok()) {
        r.status = RelayStatus::REFUNDED;
        r.refundTxHash = res.txHash;
        r.log.push_back("hoàn " + amountToString(r.message.amount) + " cho " + r.message.sender + " ở chain nguồn");
        notify(r, "refund", true, "");
    } else {
        r.log.push_back("hoàn tiền thất bại: " + res.revertReason);
        r.lastError += " | hoàn tiền thất bại: " + res.revertReason;
        notify(r, "refund", false, res.revertReason);
    }
}

std::size_t CrossChainRelayer::count(RelayStatus s) const {
    std::size_t n = 0;
    for (const auto& r : records_) n += (r.status == s);
    return n;
}

} // namespace cc
