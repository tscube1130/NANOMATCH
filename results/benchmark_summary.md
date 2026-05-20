# NANOMATCH Benchmark Summary

**Date Executed:** 2026-05-20  
**Host Architecture:** ASUSAURUS (Native Linux/WSL)  
**CPU Spec:** 16 Cores @ 3.99 GHz  
**L3 Cache Size:** 16 MB (Optimized for spatial locality)  
**Build Configuration:** CMake `Release` Mode (-O3, -march=native, -flto)

## 🧪 Benchmark Methodology & Scenarios

To ensure a rigorous and unbiased evaluation, all benchmarks pre-generate random data (Mersenne Twister `std::mt19937`) during a warmup phase. This guarantees that the Google Benchmark loops strictly measure the execution speed of the Limit Order Book and memory arena, not the CPU's random number generation.

* **Order Cancellation (O(1) Memory Test):** Tests the absolute fastest path in the engine. An order is inserted and immediately canceled. This measures the raw speed of extracting an element from the custom intrusive doubly-linked list and returning the block to the memory pool.
* **Pure Crossing (Frictionless Test):** Tests the theoretical L1 cache limit. Continuously matches aggressive buy orders directly against aggressive sell orders at the exact same price point.
* **100% Fill Rate (Execution Stress):** Tests the overhead of the lock-free `SPSCRingBuffer` logger. Every inserted order results in an immediate trade, forcing continuous cross-core communication.
* **Realistic Market (L3 Cache Stress):** The most accurate representation of live trading. Pre-loads 100,000 resting orders to create a deep, fragmented book, then sprays randomized limit orders across a realistic price distribution. This forces memory lookups outside the L1/L2 cache and tests the true latency of the memory arena.
* **Baseline STL (The Control):** Executes the exact same *Realistic Market* scenario, but replaces the custom contiguous memory arena with standard C++ `std::map` and `std::list` to demonstrate the performance penalty of OS-level heap allocations.
* **Pathological Scan (Worst-Case Flaw):** Exposes the known architectural trade-off of a flat-array price level design. Forces the engine to linearly scan thousands of empty price ticks to find the next best price, simulating an extreme spread blowout.

---

## ⏱️ Latency Percentiles (Nanoseconds)

*Measured via Google Benchmark using high-resolution OS monotonic timers backed by hardware CPU counters. All metrics reflect `real_time` wall-clock execution on pinned CPU cores (`taskset -c 0,1`).*

| Benchmark Scenario | p50 (Median) | p90 (Tail) | p99 (Extreme Tail) |
| :--- | :--- | :--- | :--- |
| **Order Cancellation** (O(1) Linked-List Extraction) | 6.76 ns | 7.31 ns | 8.93 ns |
| **Pure Crossing** (Branchless Execution) | 12.51 ns | 13.92 ns | 14.83 ns |
| **100% Fill Rate** (Execution Stress Test) | 63.49 ns | 69.42 ns | 71.44 ns |
| 🟢 **Realistic Market** (L3 Cache Stress Test) | **72.01 ns** | **73.18 ns** | **75.24 ns** |
| 🔴 **Baseline: STL `std::map`** (Realistic Market) | **150.29 ns** | **174.45 ns** | **212.24 ns** |
| **Pathological Scan** (Worst-case O(n) flaw) | 16,754 ns | 18,817 ns | 20,066 ns |
## 📊 Direct Speedup Analysis (Custom Engine vs. STL)

Under realistic market conditions, the custom contiguous memory architecture drastically outperforms standard C++ libraries, with the performance gap widening as system stress increases.

| Metric Level | NanoMatch Speed | Standard STL Speed | Speedup Multiplier |
| :--- | :--- | :--- | :--- |
| **Average Load (p50 Median)** | 72.01 ns | 150.29 ns | **2.08x Faster** |
| **High Stress (p90 Tail)** | 73.18 ns | 174.45 ns | **2.38x Faster** |
| **Market Chaos (p99 Extreme Tail)** | 75.24 ns | 212.24 ns | **2.82x Faster** |

## 🧠 Performance Conclusion
* **Zero-Allocation Hot Path:** By eliminating Operating System context switches (`malloc`/`free`/`new`) during the timing loop, the engine achieves near-deterministic tail latency. 
* **Cache Line Alignment:** The custom engine preserves the CPU's instruction pipeline. At the 99th percentile, the STL degrades by over 60 nanoseconds due to cache misses and tree rebalancing, whereas NanoMatch degrades by only **3.2 nanoseconds**.

## 🚀 High-Throughput Ingestion Test
*(Measured via independent `mmap` zero-copy ingestion script on a pre-warmed OS Page Cache)*
* **Total Orders Processed:** 5,000,000
* **Total Trades Executed & Logged:** 1,547
* **Processing Time:** 0.221 seconds

```diff
+ SUSTAINED THROUGHPUT: ~22.65 Million Orders / Second
