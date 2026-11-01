# Nhóm 10 — Khung kiểm thử & thực thi hợp đồng thông minh xuyên chuỗi (C++17)

Môi trường giả lập hướng đối tượng chạy hoàn toàn trên máy cục bộ: mô phỏng nhiều blockchain
không đồng nhất (EVM, UTXO, Solana, Mock), chuyển tài sản và cho vay xuyên chuỗi giữa chúng,
kiểm thử theo Arrange → Act → Assert, tự quay về trạng thái cũ khi kịch bản thất bại, và hiển thị
kết quả trên CLI / Dashboard / SQLite.

## Chạy nhanh

```bash
cmake -S . -B build
cmake --build build
cd build && ctest                      # 8 bộ kiểm thử, 121 phép kiểm tra

./build/test_runner                    # menu tương tác: chọn kịch bản, xem luồng thư + bảng thay đổi
./build/test_runner --run all --json dashboard/report.json --db crosschain.db
./build/test_data                      # data test: 53 bộ dữ liệu hợp lệ/biên/không hợp lệ (TH1..TH10, L1..L7)
./build/test_data --csv datatest.csv   # ghi thêm kết quả ra CSV (mở bằng Excel)
# rồi mở dashboard/index.html bằng trình duyệt
```

Yêu cầu: CMake ≥ 3.16, trình biên dịch C++17 (g++ 9+, clang 10+, MSVC 2019+). SQLite3 là tùy chọn
(có thì `test_runner --db` ghi được database). Windows: chạy `chcp 65001` để terminal hiện đúng tiếng Việt.

## Kiến trúc 5 tầng

```
Tầng 1  Giao diện & báo cáo   test_runner (CLI) · dashboard/index.html · ReportExporter · ReportRepository   ← Bước 5
          │
Tầng 2  Kiểm thử              Sandbox · SandboxSnapshot · TestCase · TestSuite · Scenarios · FlowTrace       ← Bước 4
          │
Tầng 3  Điều phối xuyên chuỗi CrossChainRelayer · MultiSigProofValidator · CrossChainBridge                  ← Bước 1, 3, 5
                              (TokenBridge · LendingBridge · UnlockBridge)
          │
Tầng 4  Kết nối blockchain    IBlockchainAdapter · EVMAdapter · UtxoAdapter · SolanaAdapter · MockChainAdapter ← Bước 1, 2
                              TransactionFactory (EVM · UTXO · Solana)
          │
Tầng 5  Sổ cái mô phỏng       ExecutionEngine · LocalLedgerSimulator · LedgerSnapshot                         ← Bước 1, 2

Dữ liệu dùng chung            CrossChainMessage · TransactionResult · SmartContractState · TxPayload          ← Bước 1
```

Một giao dịch xuyên chuỗi đi qua các lớp:

```
Người dùng ─lock─▶ Adapter nguồn ─(sự kiện Locked)─▶ CrossChainRelayer ─▶ MultiSigProofValidator (2/3 chữ ký)
                                                          │
                                                          ▼
                     CrossChainBridge::relay() ─TxPayload─▶ Adapter đích ─▶ TransactionFactory ─ITransaction─▶ ExecutionEngine ─▶ LocalLedgerSimulator
                                                          ▲                                                       │
                                                          └──────────────────── TransactionResult ◀───────────────┘
```

## Đối chiếu 5 bước của đề tài

| Bước | Yêu cầu | Đã làm | File chính |
|---|---|---|---|
| **1** | Class diagram, interface lõi, dữ liệu dùng chung | `IBlockchainAdapter`, `CrossChainBridge`, `TransactionFactory`, `LocalLedgerSimulator`; `CrossChainMessage`, `TransactionResult`, `SmartContractState` | `include/cc/core`, `adapter/IBlockchainAdapter.h`, `bridge/CrossChainBridge.h`, `ledger/` |
| **2** | Adapter & Factory cho ≥ 2 mạng, node mô phỏng | 4 adapter (EVM, UTXO, Solana, Mock), 3 factory, `ExecutionEngine` (chữ ký, gas, rollback) trên Local Memory State | `src/factory`, `src/engine`, `include/cc/adapter` |
| **3** | Relayer nghe sự kiện → kích hoạt giao dịch đích; Proof Validation | `CrossChainRelayer` (chống trùng, thử lại, hoàn tiền); `MultiSigProofValidator` M-of-N | `src/relayer`, `include/cc/bridge/MultiSigProofValidator.h` |
| **4** | Sandbox Arrange/Act/Assert; Snapshot & RevertState | `Sandbox`, `SandboxSnapshot` (chụp sổ cái + validator + relayer + mock + engine), `TestCase::run()` | `include/cc/testing`, `src/testing` |
| **5** | Ứng dụng thực tế + Test Runner (CLI/GUI) | Token Bridge (9 kịch bản) + Lending (7 kịch bản); FlowTrace; CLI; Dashboard HTML; xuất JSON; ghi SQLite | `apps/test_runner.cpp`, `dashboard/`, `src/report`, `include/cc/bridge/LendingBridge.h` |

## Mẫu thiết kế

| Mẫu | Ở đâu |
|---|---|
| Adapter | Mỗi chain một adapter cùng implement `IBlockchainAdapter` |
| Bridge | `CrossChainBridge` (Token / Lending / Unlock) tách khỏi adapter — thêm nghiệp vụ không sửa adapter, thêm chain không sửa bridge |
| Factory | `TransactionFactory::createTransaction(ChainType, Payload, ledger)` |
| Memento | `LedgerSnapshot`, `SandboxSnapshot`, `saveState()/restoreState()` của validator, relayer, mock, engine |
| Template Method | `TestCase::run()` cố định ① snapshot → ② arrange → ③ act → ④ assert → ⑤ revertState → ⑥ report |
| Observer | `listenEvent()` của adapter, `RelayObserver` của relayer → `FlowTrace` cho sơ đồ luồng thư |

## Thế giới mô phỏng & kịch bản

```
ChainA-EVM  ──Token──▶  ChainB-UTXO       TH1–TH6, TH10
ChainA-EVM  ──Token──▶  ChainM-Mock       TH7 (chain đích hỏng → hoàn tiền)
ChainA-EVM  ──Token──▶  ChainS-Solana  ┐  TH9 chuyển khứ hồi
ChainS-Solana ─Unlock─▶ ChainA-EVM     ┘
LendA-EVM   ─Lending─▶  LendS-Solana   ┐  L1–L7 cho vay xuyên chuỗi
LendS-Solana ─Unlock─▶  LendA-EVM      ┘  (trả thế chấp khi trả hết nợ / bị thanh lý)
```

| Nhóm | Kịch bản |
|---|---|
| Token Bridge | TH1 thành công · TH2 thư giả · TH3 gửi lại thư cũ · TH4 sai địa chỉ (hoàn tiền) · TH5 không đủ tiền · TH6 tiêu 1 tờ 2 lần · TH7 chain đích hỏng (hoàn tiền) · TH9 chuyển khứ hồi · TH10 nhiều người cùng chuyển |
| Lending | L1 vay trong hạn mức · L2 vay vượt hạn mức · L3 trả nợ nhận lại thế chấp · L4 trả một phần · L5 thanh lý khi giá giảm · L6 gửi lại thư trả thế chấp · L7 thanh lý khi còn an toàn |
| Kiểm tra RevertState | TH8 cố ý viết sai mong đợi → KHÔNG ĐẠT và tự quay về trạng thái cũ |

Lending: thế chấp ở `LendA-EVM`, vay ở `LendS-Solana`. Quỹ cho vay là két bridge trên Solana; oracle giá
giả lập là slot `priceX100` (mặc định 100 = 1:1), LTV 50% (`ltvBps`), ngưỡng thanh lý 80% (`liqBps`).

## Giao diện

**CLI** — `test_runner`: không tham số là menu tương tác; `--list`, `--run all|1,3`, `--json <file>`,
`--db <file>`, `--schema <file>`, `-q`. Xem chi tiết một kịch bản: luồng thư từng bước, bảng thay đổi,
từng phép so.

**Dashboard** — `dashboard/index.html` đọc `dashboard/report.js` (do `test_runner --json` tạo).

Tab *Thực hành* (mở đầu tiên) là một sàn giao dịch xuyên chuỗi thu nhỏ chạy ngay trong trình duyệt,
mô phỏng lại đúng luật của engine C++. Giao diện kiểu sàn thật: thanh giá ETH/SOL (giá khớp, % thay đổi,
cao/thấp, khối lượng), dải chữ chạy các lệnh khớp, *sổ lệnh* hai phía có thanh độ sâu, *biểu đồ nến* kèm
khối lượng, danh sách *lệnh khớp gần đây* với trạng thái thanh toán, khung *Mua / Bán* (giá, số lượng,
25–100 %, giá tốt nhất) và bảng *Lệnh đang mở* có nút Hủy. Lệnh mua khóa SOL vào ký quỹ sàn trên ChainS,
lệnh bán khóa ETH vào ký quỹ trên ChainA; khớp giá theo ưu tiên giá → thời gian, có chống tự khớp. Mỗi lệnh
khớp sinh 2 thư `release` qua relayer (2/3 validator): ChainA trả ETH cho người mua, ChainS trả SOL cho
người bán — một chain hỏng thì lệnh khớp bị *LỆCH*. Nút *Thị trường tự chạy* cho bot đặt lệnh quanh giá
hợp lý (bật kèm chế độ tự động). Mỗi người là một *thẻ ví* (ETH, wETH, SOL, wSOL) với số dư nhảy số,
nhãn +/−, thanh so sánh, biểu đồ số dư theo thời gian và dòng "đang chờ / đang tới"; đồng xu bay từ ví vào
két rồi từ relayer sang ví người nhận; bảng *Biến động số dư* ghi mỗi lần token của mọi người thay đổi.
Tự gõ số dư ETH / SOL cho từng người, thêm người; nút nhanh 10 / 50 / 100 / Một nửa / Tất cả; ra lệnh
*Chuyển tiền* (tiền gốc → khóa vào két, chain kia phát token bọc; token bọc → đốt, chain kia mở khóa)
hoặc *Đổi chéo 2 bên* (bên 1 đưa ETH, bên 2 đưa SOL); lệnh vào hàng chờ và chỉ thực thi khi bấm
*Đóng khối*; *Relayer chạy 1 vòng* thử mỗi thư một lần (cần 2/3 validator, tối đa 3 lần, hết lượt thì
hoàn tiền); bật / tắt từng validator, *Làm chain này hỏng*; 9 nút *Thử tình huống* (bình thường, nhiều
người, không đủ tiền, tiêu 2 lần, sai địa chỉ, chain hỏng, thư giả, gửi lại thư cũ, validator sập);
*Chụp trạng thái* / *Quay lại ảnh chụp* (Snapshot / RevertState); chỉ báo *Bảo toàn tiền* (token bọc
đang lưu hành phải bằng tiền trong két); bảng thư và nhật ký giải thích từng việc.

Tab *Giải thích*: mỗi tình huống là một nút bấm đặt tên dễ hiểu; bấm vào sẽ thấy các chain,
Relayer, Validator và một "lá thư" chạy giữa chúng, với nút *Bước tiếp / Bước trước / Tự chạy / Xem lại
từ đầu* (phím ← → cũng dùng được); mỗi bước có một câu giải thích, cuối cùng là "Ai chặn, vì sao" và
"Kết quả sau cùng". Tab *Test Runner*: chọn kịch bản, sơ đồ tuần tự luồng thư giữa các chain, bảng State Changes, bảng Assert,
bảng thư xuyên chuỗi. Tab *Kiến trúc*: 5 tầng, bản đồ chain & tuyến, mẫu thiết kế. Không có `report.js`
thì trang cho chọn file `report.json`.

**SQLite** — `test_runner --db crosschain.db` ghi theo `data/schema.sql`: `test_cases`, `test_runs`,
`assertion_results`, `state_changes`, `cross_chain_messages`, `chains`; view `v_test_summary` thống kê.

## Cấu trúc thư mục

```
include/cc/  core/ adapter/ factory/ engine/ ledger/ bridge/ relayer/ testing/ report/
src/         factory/ engine/ adapter/ relayer/ testing/ report/
apps/        test_runner.cpp (CLI Bước 5) · run_scenarios.cpp (in báo cáo Bước 4)
tests/       step1_interfaces_test · step2_adapters_test · step2_spec_test · step3_relayer_test
             step4_sandbox_test · step5_app_test
dashboard/   index.html · report.json · report.js
data/        schema.sql · seed.sql
```

## Ghi chú thiết kế

- **Node mô phỏng là Local Memory State.** C++ không có ethers.js / Solana Web3 SDK; adapter bọc
  `LocalLedgerSimulator`. Nối Hardhat/Ganache thật = viết adapter gọi JSON-RPC, các tầng khác giữ nguyên.
- **Chữ ký, băm, proof là giả lập** (`simSign`, `simHash`) — chỉ dùng cho môi trường kiểm thử.
- **Số tiền** mặc định `uint64_t`; build với `-DCC_USE_BOOST=ON` (cần Boost) để dùng số 256-bit như EVM.
- **Không dùng Singleton** cho sổ cái: mỗi chain một `LocalLedgerSimulator` để các test độc lập.
- **RevertState khôi phục mọi thành phần**, không chỉ sổ cái — nếu không, validator/relayer còn nhớ
  thư cũ và chặn nhầm lần chạy lại (xem `step4_sandbox_test`, phần B).
- **Thư có con dấu sai** (REJECTED) không được thử lại và không tự hoàn tiền vì có thể là giả mạo.
