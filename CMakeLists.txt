cmake_minimum_required(VERSION 3.16)
project(WattWise VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Core library: everything except the CLI, so tests link the same code.
add_library(wattwise_core
  src/common.cpp
  src/lexer.cpp            # M1
  src/parser.cpp           # M1
  src/semantic.cpp         # M1
  src/ir.cpp               # M2
  src/irgen.cpp            # M2
  src/cfg.cpp              # M2
  src/energy_model.cpp     # M2
  src/frequency.cpp        # M3·M4
  src/analysis.cpp         # M3
  src/optimize/strength.cpp  # M3
  src/optimize/cse.cpp       # M3
  src/optimize/licm.cpp      # M3
  src/interpreter.cpp      # M4
  src/report.cpp           # M4
)
target_include_directories(wattwise_core PUBLIC include)

add_executable(wattwise src/main.cpp)
target_link_libraries(wattwise PRIVATE wattwise_core)

option(WATTWISE_TESTS "Build unit tests" ON)
if(WATTWISE_TESTS)
  enable_testing()
  add_library(catch2_main STATIC third_party/catch_amalgamated.cpp)
  target_include_directories(catch2_main PUBLIC third_party)
  add_executable(wattwise_tests
    tests/unit/test_lexer.cpp
    tests/unit/test_parser_semantic.cpp
    tests/unit/test_ir_cfg.cpp
    tests/unit/test_energy.cpp
    tests/unit/test_passes.cpp)
  target_link_libraries(wattwise_tests PRIVATE wattwise_core catch2_main)
  add_test(NAME unit COMMAND wattwise_tests)
  add_test(NAME differential
           COMMAND ${CMAKE_SOURCE_DIR}/scripts/difftest.sh $<TARGET_FILE:wattwise>
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME invalid_programs
           COMMAND ${CMAKE_SOURCE_DIR}/scripts/invalidtest.sh $<TARGET_FILE:wattwise>
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
