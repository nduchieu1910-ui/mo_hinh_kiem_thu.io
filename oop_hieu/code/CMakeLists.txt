cmake_minimum_required(VERSION 3.16)
project(CrossChainTestingFramework LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(CC_USE_BOOST "Dùng boost::multiprecision cho số tiền 256-bit" OFF)

if(MSVC)
  set(CC_WARN /W4 /utf-8)
else()
  set(CC_WARN -Wall -Wextra -Wpedantic)
endif()

# ---- Bước 1: interface + dữ liệu dùng chung (chỉ header) -------------
add_library(cc_core INTERFACE)
target_include_directories(cc_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(CC_USE_BOOST)
  find_package(Boost REQUIRED)
  target_compile_definitions(cc_core INTERFACE CC_USE_BOOST)
  target_link_libraries(cc_core INTERFACE Boost::headers)
endif()

# ---- Bước 2–5: Factory + ExecutionEngine + Adapter + Relayer + Sandbox + Report --------------------------------------
add_library(cc_framework STATIC
  src/factory/TransactionFactory.cpp
  src/factory/EVMTransactionFactory.cpp
  src/factory/UtxoTransactionFactory.cpp
  src/factory/SolanaTransactionFactory.cpp
  src/engine/ExecutionEngine.cpp
  src/adapter/MockChainAdapter.cpp
  src/relayer/CrossChainRelayer.cpp
  src/testing/Sandbox.cpp
  src/testing/TestCase.cpp
  src/testing/Scenarios.cpp
  src/report/ReportExporter.cpp
)
target_link_libraries(cc_framework PUBLIC cc_core)
target_compile_options(cc_framework PRIVATE ${CC_WARN})

# ---- Test ------------------------------------------------------------
enable_testing()

add_executable(step1_interfaces_test tests/step1_interfaces_test.cpp)
target_link_libraries(step1_interfaces_test PRIVATE cc_core)
target_compile_options(step1_interfaces_test PRIVATE ${CC_WARN})
add_test(NAME step1_interfaces_test COMMAND step1_interfaces_test)

add_executable(step2_adapters_test tests/step2_adapters_test.cpp)
target_link_libraries(step2_adapters_test PRIVATE cc_framework)
target_compile_options(step2_adapters_test PRIVATE ${CC_WARN})
add_test(NAME step2_adapters_test COMMAND step2_adapters_test)

add_executable(step2_spec_test tests/step2_spec_test.cpp)
target_link_libraries(step2_spec_test PRIVATE cc_framework)
target_compile_options(step2_spec_test PRIVATE ${CC_WARN})
add_test(NAME step2_spec_test COMMAND step2_spec_test)

add_executable(step3_relayer_test tests/step3_relayer_test.cpp)
target_link_libraries(step3_relayer_test PRIVATE cc_framework)
target_compile_options(step3_relayer_test PRIVATE ${CC_WARN})
add_test(NAME step3_relayer_test COMMAND step3_relayer_test)

add_executable(step4_sandbox_test tests/step4_sandbox_test.cpp)
target_link_libraries(step4_sandbox_test PRIVATE cc_framework)
target_compile_options(step4_sandbox_test PRIVATE ${CC_WARN})
add_test(NAME step4_sandbox_test COMMAND step4_sandbox_test)

# Chương trình chạy 7 kịch bản và in báo cáo
add_executable(run_scenarios apps/run_scenarios.cpp)
target_link_libraries(run_scenarios PRIVATE cc_framework)
target_compile_options(run_scenarios PRIVATE ${CC_WARN})
add_test(NAME run_scenarios COMMAND run_scenarios)

# Data test: 53 bộ dữ liệu hợp lệ / biên / không hợp lệ theo TH1..TH10, L1..L7
add_executable(test_data apps/test_data.cpp)
target_link_libraries(test_data PRIVATE cc_framework)
target_compile_options(test_data PRIVATE ${CC_WARN})
add_test(NAME test_data COMMAND test_data -q)

# ---- Bước 5: ghi báo cáo vào SQLite (nếu máy có thư viện SQLite3) ----
find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
  add_library(cc_report_db STATIC src/report/ReportRepository.cpp)
  target_link_libraries(cc_report_db PUBLIC cc_framework SQLite::SQLite3)
  target_compile_definitions(cc_report_db PUBLIC CC_HAVE_SQLITE)
  target_compile_options(cc_report_db PRIVATE ${CC_WARN})
  message(STATUS "SQLite3: có — test_runner sẽ ghi được vào crosschain.db")
else()
  message(STATUS "SQLite3: không tìm thấy — test_runner bỏ qua phần ghi database")
endif()

# ---- Bước 5: CLI Test Runner ----
add_executable(test_runner apps/test_runner.cpp)
target_compile_options(test_runner PRIVATE ${CC_WARN})
if(SQLite3_FOUND)
  target_link_libraries(test_runner PRIVATE cc_report_db)
else()
  target_link_libraries(test_runner PRIVATE cc_framework)
endif()
add_test(NAME test_runner_all COMMAND test_runner --run all -q)

add_executable(step5_app_test tests/step5_app_test.cpp)
target_compile_options(step5_app_test PRIVATE ${CC_WARN})
if(SQLite3_FOUND)
  target_link_libraries(step5_app_test PRIVATE cc_report_db)
else()
  target_link_libraries(step5_app_test PRIVATE cc_framework)
endif()
target_compile_definitions(step5_app_test PRIVATE CC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}" CC_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME step5_app_test COMMAND step5_app_test)
