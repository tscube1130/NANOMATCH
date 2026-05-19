cmake_minimum_required(VERSION 3.14)
project(NanoMatch)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
# Turn on C++20 and Extreme Hardware Optimizations
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -flto")

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Disable benchmark tests" FORCE)

# Automatically download Google Benchmark
include(FetchContent)
FetchContent_Declare(
  benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.8.3
)
FetchContent_MakeAvailable(benchmark)

# ---------------------------------------------------------
# TARGET 1: The Google Benchmark Suite (Latency)
# ---------------------------------------------------------
add_executable(nano_bench benchmark.cpp)
target_link_libraries(nano_bench PRIVATE benchmark::benchmark pthread)

# ---------------------------------------------------------
# TARGET 2: The Zero-Copy mmap Ingestion Test (Throughput)
# ---------------------------------------------------------
add_executable(ingest_test ingest.cpp)

# This perfectly mimics our -DINGEST_BUILD flag to prevent the "Double Main" error!
target_compile_definitions(ingest_test PRIVATE INGEST_BUILD)
target_link_libraries(ingest_test PRIVATE pthread)