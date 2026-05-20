# NanoMatch: Ultra-Low Latency Limit Order Book (LOB)

![C++20](https://img.shields.io/badge/C++-20-blue.svg) ![CMake](https://img.shields.io/badge/CMake-Release-success.svg) ![Linux](https://img.shields.io/badge/OS-Linux%20%2F%20WSL-orange.svg)

## ⚡ Executive Summary
Standard algorithmic trading strategies often focus purely on alpha generation. However, in the High-Frequency Trading (HFT) industry, the most brilliant alpha model is useless if the underlying execution system is a microsecond too slow. 

**NanoMatch** is a C++20 Limit Order Book (LOB) built from scratch with absolute hardware sympathy. Designed to bridge the gap between theoretical quantitative finance and hardcore systems engineering, this engine abandons standard STL containers in favor of custom memory arenas. It delivers deterministic, <mark>sub-100-nanosecond</mark> execution, demonstrating the exact microarchitectural mindset demanded by top-tier proprietary trading firms.

---

## 🏗️ Architecture & Data Structure Design
To achieve median latencies of <mark>**~72 nanoseconds**</mark>, we engineered the data structures specifically for the CPU's L1/L2 cache, completely bypassing the Operating System's memory manager during runtime.

1. **The Intrusive Doubly-Linked List:** Standard `std::list` allocates nodes randomly on the heap, destroying cache locality. In NanoMatch, the `next` and `prev` pointers are replaced by integer indices that live directly inside a cache-aligned `Order` struct. This allows $O(1)$ order cancellation without triggering a single pointer dereference or cache miss.
2. **The Pre-Allocated Memory Arena (Object Pool):** OS context switches (`malloc`, `free`, `new`, `delete`) are catastrophic for tail latency. NanoMatch pre-allocates a massive `std::array` of millions of `Order` blocks at startup. Allocating or freeing an order is reduced to an $O(1)$ integer pop/push from a lock-free free-list.
3. **Flat Array Price Levels (Direct Addressing):** Instead of using an $O(\log n)$ Red-Black tree (`std::map`) to track price levels, we utilize a flat, statically sized array indexed directly by the price tick. Finding the head of a price level is an $O(1)$ array lookup.

---

## 🧠 Hardware Sympathy & Microarchitectural Optimizations
* **Cache-Line Alignment:** Critical structs (`Order`, `PriceLevel`) are packed to prevent crossing 64-byte L1 cache-line boundaries. This ensures spatial locality and prevents false sharing when the background logger thread reads memory.
* **Instruction Pipeline Preservation:** The crossing logic avoids branching where possible. In the benchmarking harness, expensive modulo operators (`%`) were replaced with 1-cycle bitwise AND masks (`&`) to prevent the CPU's integer divider from stalling the instruction pipeline.
* **Zero-Copy I/O (`mmap`):** Market data ingestion bypasses standard file streams, utilizing OS-level memory mapping to load multi-million row datasets directly into virtual RAM, achieving parsing throughputs exceeding <mark>**22 Million Orders / Second**</mark>.

---

## 🛡️ Benchmark Integrity (Defeating the "Cheats")
Microbenchmarking is notoriously prone to "cheating" via unrealistic conditions. Our benchmark suite (powered by Google Benchmark) was specifically designed to enforce mathematical honesty:

* **Avoiding "Pre-fill Exhaustion":** We do not benchmark an empty book. The `Realistic Market` scenario pre-fills 100,000 orders across a wide price distribution, ensuring that crossing logic is genuinely executed on every single iteration.
* **Avoiding the "Single Price Level" Trap:** We spray randomized limit orders across a 400-tick spread using pre-generated Mersenne Twister distributions. This forces the engine to access disparate memory locations, proving its speed under genuine L2/L3 cache pressure rather than relying on an artificially hot L1 cache.
* **Isolating Operations:** We do not average "mixed operations." Crossing, $O(1)$ Cancellations, and Pathological worst-case scenarios are explicitly isolated into separate Google Benchmark runs to provide actionable percentile data.
* **The Apples-to-Apples Baseline:** We benchmarked our engine against an identical market simulation using standard C++ `std::map` and `std::list`, proving a <mark>**>2.8x speedup in 99th percentile tail latency**.

---

## 📊 Performance & Profiling Proofs

NanoMatch is rigorously tested on pinned CPU cores (`taskset -c 0,1`) to eliminate Hypervisor and OS scheduler noise. 

### 📈 See the Full Benchmark Report
For exact latency percentiles (p50, p90, p99) and sustained throughput metrics, read the full report here: 
👉 **[`results/benchmark_summary.md`](results/benchmark_summary.md)**

### 🔬 Visual Profiling (The "Receipts")
Inside the [`results/`](results/) directory, you will find visual profiling evidence proving our zero-allocation hot path:
* **`flamegraph_custom_engine.png`**: Proves the elimination of memory allocation stalls. The stack depth is shallow and branch-predictable.
* **`flamegraph_stl_baseline.png`**: Exposes the devastating cost of `std::map`, showing the CPU stalling on deep stack frames waiting for Red-Black Tree rebalancing and heap allocations.

---

## 🛠️ Build & Reproduction Instructions

**Tech Stack:** C++17/20, CMake, Google Benchmark, Linux `perf` / `mmap`.

```bash
# 1. Clone the repository
git clone [https://github.com/tscube1130/NANOMATCH.git](https://github.com/tscube1130/NANOMATCH.git)
cd NANOMATCH

# 2. Generate the synthetic order dataset
python3 generate_orders.py

# 3. Generate the build system (Release mode is forced)
cmake -S . -B build

# 4. Compile the targets
cmake --build build -j

# 5. Run the Latency Benchmarks (We recommend pinning to a core)
taskset -c 0,1 ./build/nano_bench

# 6. Run the Zero-Copy Throughput Test
./build/ingest_test
```

## 🔥 Generating Flame Graphs (Linux Perf & Speedscope)

To independently verify the zero-allocation hot path and L1/L2 cache locality, you can reproduce the flame graphs using the Linux `perf` tool and the **Speedscope** web visualizer.

#### **Phase 1: Recording the Data**
Run the following commands in your terminal to profile the compiled benchmark binary:

**1. Record the stack traces** (at 99 Hertz):
```bash
sudo perf record -F 99 -g -- ./build/nano_bench
```

**2. View an interactive breakdown** *(Optional terminal view)*:
```bash
sudo perf report
```

**3. Export the binary data** (into a human-readable text file):
```bash
sudo perf script > profile.txt
```

---

#### **Phase 2: Visualizing in the Browser**
You do not need to install any local visualization packages to view the stack traces. 

1. **Navigate:** Open [speedscope.app](https://www.speedscope.app/) in your web browser.
2. **Upload:** Drag and drop your generated `profile.txt` file into the browser window.
3. **Analyze:** Select the **"Left Heavy"** or **"Sandwich"** view at the top of the screen. 

> **Tip:** Use these views to easily identify exact microarchitectural bottlenecks and verify the absence of OS-level memory allocations (like `malloc` or `operator new`).
