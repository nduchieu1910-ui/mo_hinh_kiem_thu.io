#include "cc/factory/TransactionFactory.h"

#include "cc/factory/EVMTransactionFactory.h"
#include "cc/factory/SolanaTransactionFactory.h"
#include "cc/factory/UtxoTransactionFactory.h"

namespace cc {

std::unique_ptr<TransactionFactory>
TransactionFactory::forChain(ChainType type, std::shared_ptr<const LocalLedgerSimulator> ledger) {
    switch (type) {
        case ChainType::EVM:    return std::make_unique<EVMTransactionFactory>(std::move(ledger));
        case ChainType::UTXO:   return std::make_unique<UtxoTransactionFactory>(std::move(ledger));
        case ChainType::SOLANA: return std::make_unique<SolanaTransactionFactory>(std::move(ledger));
        case ChainType::MOCK:   break;   // Mock không cần factory
    }
    throw std::invalid_argument(std::string("Chưa có factory cho loại chain: ") + toString(type));
}

} // namespace cc
