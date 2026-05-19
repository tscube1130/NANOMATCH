cmake_minimum_required(VERSION 3.14)
project(NanoMatch)

# Turn on C++20 and Extreme Hardware Optimizations
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -flto")

# THE FIX: Tell Google Benchmark NOT to download/build its own tests
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Disable benchmark tests" FORCE)

# Automatically download Google Benchmark
include(FetchContent)
FetchContent_Declare(
  benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.8.3
)
FetchContent_MakeAvailable(benchmark)

# Create our benchmark executable
add_executable(nano_bench benchmark.cpp)
target_link_libraries(nano_bench PRIVATE benchmark::benchmark pthread)