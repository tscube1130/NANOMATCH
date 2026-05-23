cmake_minimum_required(VERSION 3.14)
project(NanoMatch)

set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)

# C++20 + maximum hardware optimisations
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -flto")

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Disable benchmark tests" FORCE)

# Auto-download Google Benchmark v1.8.3
include(FetchContent)
FetchContent_Declare(
  benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG        v1.8.3
)
FetchContent_MakeAvailable(benchmark)

# ---------------------------------------------------------
# TARGET 1: Google Benchmark Suite (Latency)
# ---------------------------------------------------------
add_executable(nano_bench benchmark.cpp)
target_link_libraries(nano_bench PRIVATE benchmark::benchmark pthread)

# ---------------------------------------------------------
# TARGET 2: Core Matching Speed — no trade log overhead
#           Run: ./ingest_fast
# ---------------------------------------------------------
add_executable(ingest_fast ingest.cpp)
target_link_libraries(ingest_fast PRIVATE pthread)
# No -DENABLE_LOGGING: engine runs with nullptr trade buffer

# ---------------------------------------------------------
# TARGET 3: End-to-End Pipeline — matching + concurrent logger
#           Run: ./ingest_e2e
# ---------------------------------------------------------
add_executable(ingest_e2e ingest.cpp)
target_compile_definitions(ingest_e2e PRIVATE ENABLE_LOGGING)
target_link_libraries(ingest_e2e PRIVATE pthread)