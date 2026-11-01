#pragma once
// =====================================================================
//  Kịch bản kiểm thử
//
//  Thế giới chuẩn (buildStandardWorld):
//     ChainA-EVM  ──Token──▶  ChainB-UTXO     (tuyến chính, 7 trường hợp Bước 1)
//     ChainA-EVM  ──Token──▶  ChainM-Mock     (chain giả để gây lỗi)
//     ChainA-EVM  ──Token──▶  ChainS-Solana   ┐ chuyển khứ hồi
//     ChainS-Solana ─Unlock─▶ ChainA-EVM      ┘
//     LendA-EVM   ─Lending─▶  LendS-Solana    ┐ ứng dụng cho vay xuyên chuỗi
//     LendS-Solana ─Unlock──▶ LendA-EVM       ┘ (trả thế chấp khi trả nợ / thanh lý)
//  Mỗi tuyến: 3 validator, cần 2 chữ ký; relayer thử tối đa 3 lần.
// =====================================================================
#include "cc/testing/TestCase.h"

#include <memory>
#include <vector>

namespace cc::scenarios {

inline const char* kChainA = "ChainA-EVM";
inline const char* kChainB = "ChainB-UTXO";
inline const char* kChainM = "ChainM-Mock";
inline const char* kChainS = "ChainS-Solana";
inline const char* kLendA  = "LendA-EVM";
inline const char* kLendS  = "LendS-Solana";
inline const char* kAlice  = "0xa11ce";
inline const char* kEscrow = "0xb41d6e";

// Địa chỉ Solana 32 ký tự base58 dùng trong kịch bản.
Address solAddr(const std::string& name);
const Address& kAliceSol();
const Address& kSolVault();      // két bridge / quỹ cho vay trên Solana

void buildStandardWorld(Sandbox& sb);

std::vector<std::unique_ptr<TestCase>> standardScenarios();      // TH1 .. TH7  (Bước 1 – 4)
std::vector<std::unique_ptr<TestCase>> tokenAppScenarios();      // TH9 khứ hồi, TH10 nhiều người
std::vector<std::unique_ptr<TestCase>> lendingScenarios();       // L1 .. L7   (Bước 5)
std::unique_ptr<TestCase> intentionallyFailingScenario();        // TH8 mong đợi sai có chủ đích

// Tất cả kịch bản theo thứ tự hiển thị trên Test Runner.
std::vector<std::unique_ptr<TestCase>> allScenarios(bool includeFailing = true);

} // namespace cc::scenarios
