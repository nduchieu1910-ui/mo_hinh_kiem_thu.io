#include "cc/factory/EVMTransactionFactory.h"

#include "cc/core/HashUtil.h"
#include "cc/ledger/LocalLedgerSimulator.h"

#include <cctype>

namespace cc {

EVMTransactionFactory::EVMTransactionFactory(std::shared_ptr<const LocalLedgerSimulator> ledger,
                                             Amount defaultGasPrice)
    : ledger_(std::move(ledger)), defaultGasPrice_(defaultGasPrice) {
    if (!ledger_) throw std::invalid_argument("EVMTransactionFactory: thiếu sổ cái");
}

bool EVMTransactionFactory::isValidAddress(const Address& a) {
    if (a.size() < 3 || a.size() > 42 || a.rfind("0x", 0) != 0) return false;
    for (std::size_t i = 2; i < a.size(); ++i)
        if (!std::isxdigit(static_cast<unsigned char>(a[i]))) return false;
    return true;
}

std::uint64_t EVMTransactionFactory::defaultGasLimit(const std::string& action) {
    if (action == "transfer") return 21000;
    if (action == "lock" || action == "unlock") return 60000;
    if (action == "mint") return 50000;
    return 100000;  // gọi hợp đồng chung
}

std::unique_ptr<ITransaction> EVMTransactionFactory::createTransaction(const TxPayload& p) {
    if (p.action.empty()) throw TxBuildError("EVM: thiếu hành động (action)");

    // mint do hệ thống (bridge) phát ra; nếu không nêu người gửi thì dùng 0x0
    const Address from = p.from.empty() ? Address("0x0") : p.from;
    const Address to = p.to.empty() && (p.action == "lock" || p.action == "repay") ? from : p.to;  // tiền vào két / quỹ
    if (!isValidAddress(from)) throw TxBuildError("EVM: địa chỉ người gửi sai mẫu: " + from);
    if (!isValidAddress(to)) throw TxBuildError("EVM: địa chỉ người nhận sai mẫu: " + to);
    if (p.action != "call" && p.amount == 0) throw TxBuildError("EVM: số tiền phải lớn hơn 0");

    auto tx = std::make_unique<EvmTransaction>();
    tx->from = from;
    tx->to = to;
    tx->value = p.amount;

    // nonce = số giao dịch tài khoản đã gửi thành công
    auto acc = ledger_->getAccount(from);
    tx->nonce = acc ? acc->nonce : 0;

    auto it = p.args.find("gasPrice");
    tx->gasPrice = it != p.args.end() ? amountFromString(it->second) : defaultGasPrice_;
    it = p.args.find("gasLimit");
    tx->gasLimit = it != p.args.end() ? std::stoull(it->second) : defaultGasLimit(p.action);

    // data = "action|k=v|k=v" (giả lập ABI encode)
    std::string data = p.action;
    for (const auto& [k, v] : p.args)
        if (k != "gasPrice" && k != "gasLimit") data += "|" + k + "=" + v;
    tx->data.assign(data.begin(), data.end());

    tx->txHash = simHash(tx->from + "|" + tx->to + "|" + amountToString(tx->value) + "|" +
                         std::to_string(tx->nonce) + "|" + data);
    tx->signature = simSign(tx->from, tx->txHash);   // ExecutionEngine sẽ kiểm lại
    return tx;
}

} // namespace cc
