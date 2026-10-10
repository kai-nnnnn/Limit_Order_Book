cmake_minimum_required(VERSION 3.20)
project(orderbook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # lets clangd / VS Code understand the project

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---- core library -----------------------------------------------------------
add_library(orderbook src/order_book.cpp)
target_include_directories(orderbook PUBLIC include)
target_compile_options(orderbook PRIVATE -Wall -Wextra -Wpedantic)

# ---- command-line driver ----------------------------------------------------
add_executable(lob_cli src/main.cpp)
target_link_libraries(lob_cli PRIVATE orderbook)

# ---- benchmark --------------------------------------------------------------
add_executable(lob_bench bench/bench_main.cpp)
target_link_libraries(lob_bench PRIVATE orderbook)

# ---- tests (GoogleTest is downloaded at configure time) ---------------------
option(ORDERBOOK_BUILD_TESTS "Build unit tests" ON)
if(ORDERBOOK_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    add_executable(orderbook_tests
        tests/order_book_test.cpp
        tests/randomized_test.cpp
    )
    target_link_libraries(orderbook_tests PRIVATE orderbook GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(orderbook_tests)
endif()
