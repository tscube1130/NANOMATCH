# NANOMATCH Benchmark Summary

**Date Executed:** 2026-05-20  
**Host Architecture:** ASUSAURUS (Native Linux/WSL)  
**CPU Spec:** 16 Cores @ 3.99 GHz  
**L3 Cache Size:** 16 MB (Optimized for spatial locality)  
**Build Configuration:** CMake `Release` Mode (-O3, -march=native, -flto)

## Latency Percentiles (Nanoseconds)

*Measured via Google Benchmark using high-resolution OS monotonic timers backed by hardware CPU counters. All metrics reflect `real_time` wall-clock execution on pinned CPU cores (`taskset -c 0,1`).*

| Benchmark Scenario | p50 (Median) | p90 (Tail) | p99 (Extreme Tail) |
| :--- | :--- | :--- | :--- |
| **Order Cancellation** ($O(1)$ Linked-List Extraction) | 6.76 ns | 7.31 ns | 8.93 ns |
| **Pure Crossing** (Branchless Execution) | 12.51 ns | 13.92 ns | 14.83 ns |
| **100% Fill Rate** (Execution Stress Test) | 63.49 ns | 69.42 ns | 71.44 ns |
| 🟢 **Realistic Market** (L3 Cache Stress Test) | **72.01 ns** | **73.18 ns** | **75.24 ns** |
| 🔴 **Baseline: STL `std::map`** (Realistic Market) | **150.29 ns** | **174.45 ns** | **212.24 ns** |
| **Pathological Scan** (Worst-case $O(n)$ flaw) | 16,754 ns | 18,817 ns | 20,066 ns |

## 🧠 Performance Conclusion
The custom memory-arena architecture (`BM_Realistic_Market`) completed order matching and routing in **~72.0 ns** (median). The identically simulated market using standard C++ dynamically allocated structures (`BM_Baseline_STL`) required **~150.3 ns**. 

By eliminating Operating System context switches (`malloc`/`free`) and aligning structures to hardware cache lines, **NanoMatch operates 2.08x faster than standard STL implementations**, providing strict deterministic latency even in the 99th percentile (75.24 ns).

## 🚀 Throughput Ingestion Test
*(Measured via independent `mmap` zero-copy ingestion script)*
* **Total Orders Processed:** 5,000,000
* **Total Trades Executed & Logged:** 1,759
* **Processing Time:** 0.242 seconds

```diff
+ SUSTAINED THROUGHPUT: ~20.64 Million Orders / Second