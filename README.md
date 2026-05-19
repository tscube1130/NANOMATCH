# NanoMatch: Ultra-Low Latency Limit Order Book (LOB)

![C++20](https://img.shields.io/badge/C++-20-blue.svg) ![CMake](https://img.shields.io/badge/CMake-Release-success.svg) ![Linux](https://img.shields.io/badge/OS-Linux%20%2F%20WSL-orange.svg)

## ⚡ Executive Summary
Standard algorithmic trading strategies often focus purely on alpha generation. However, in the High-Frequency Trading (HFT) industry, the most brilliant alpha model is completely useless if the underlying execution system is a microsecond too slow. 

**NanoMatch** is a C++20 Limit Order Book (LOB) built from scratch with absolute hardware sympathy. By stripping away high-level abstractions like `std::map` and dynamic memory allocations, this project bridges the gap between theoretical quantitative finance and hardcore, low-level systems engineering. It is designed for deterministic, sub-microsecond execution to demonstrate the exact architectural mindset demanded by top-tier proprietary trading firms.

## 🧠 Microarchitectural Optimizations
To achieve median latencies below 100 nanoseconds, standard C++ practices were abandoned in favor of aggressive, hardware-aware optimizations:

* **Zero-Allocation Hot Path:** The OS `malloc`/`free` context switches are entirely bypassed. The engine utilizes a custom `std::array` memory arena, initializing millions of order blocks at startup.
* **Cache-Line Alignment & False Sharing Prevention:** Critical data structures are packed and padded to fit exactly within 64-byte L1 cache lines, ensuring spatial locality and preventing CPU cache invalidation across threads.
* **Branch Misprediction Avoidance:** The core crossing logic is written to minimize conditional branching to ensure the CPU's Branch Predictor Unit (BPU) remains flushed and the instruction pipeline stays full.
* **Zero-Copy I/O:** Market data ingestion completely bypasses the standard file stream overhead by using OS-level `mmap` to map multi-million row order datasets directly into virtual memory.

## 📊 Performance & Profiling Proofs

NanoMatch is rigorously benchmarked against standard STL baseline implementations. The custom architecture operates **nearly 3x faster** than a dynamically allocated `std::map` order book, achieving sustained throughputs exceeding **20 Million Orders / Second**.

### 📈 See the Full Benchmark Report
For exact latency percentiles (p50, p90, p99) measured via high-resolution OS monotonic timers, as well as sustained zero-copy throughput metrics, please read the full report here: 
👉 **[`results/benchmark_summary.md`](results/benchmark_summary.md)**

### 🔬 Visual Profiling (The "Receipts")
Performance claims mean nothing without microarchitectural proof. Inside the [`results/`](results/) directory, you will find visual profiling evidence proving the resolution of L1/L2 cache misses and OS overhead:
* **`flamegraph_custom_engine.png`**: Linux Perf flame graph showing the elimination of memory allocation stalls in the NanoMatch hot path.
* **`flamegraph_stl_baseline.png`**: Linux Perf flame graph demonstrating the catastrophic cache-miss penalty of standard C++ maps.

## 🛠️ Build & Reproduction Instructions

**Tech Stack:** C++17/20, CMake, Google Benchmark, perf / mmap.

```bash
# 1. Clone the repository
git clone https://github.com/tscube1130/NANOMATCH.git
cd NANOMATCH

# 2. Generate the build system (Release mode is forced via CMakeLists)
cmake -S . -B build

# 3. Compile the targets
cmake --build build -j

# 4. Run the Latency Benchmarks
./build/nano_bench

# 5. Run the Zero-Copy Throughput Test
./build/ingest_test
