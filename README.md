# NanoMatch: Ultra-Low Latency Limit Order Book (LOB)

![C++20](https://img.shields.io/badge/C++-20-blue.svg) ![CMake](https://img.shields.io/badge/CMake-Release-success.svg) ![Linux](https://img.shields.io/badge/OS-Linux%20%2F%20WSL2-orange.svg) ![Google Benchmark](https://img.shields.io/badge/Benchmarked-Google%20Benchmark-informational.svg)

---

## ⚡ Executive Summary

Standard algorithmic trading strategies often focus purely on alpha generation. However, in the High-Frequency Trading (HFT) industry, the most brilliant alpha model is useless if the underlying execution system is a microsecond too slow.

**NanoMatch** is a C++20 Limit Order Book (LOB) built from scratch with absolute hardware sympathy. Designed to bridge the gap between theoretical quantitative finance and hardcore systems engineering, this engine abandons standard STL containers in favour of custom memory arenas. It delivers deterministic, sub-100-nanosecond execution — demonstrating the exact microarchitectural mindset demanded by top-tier proprietary trading firms.

---

## 🏆 Key Results at a Glance

> All numbers measured on AMD Ryzen 9 8945HS (Zen 4) · 8C/16T · WSL2 · `-O3 -march=native -flto`. Full methodology in [`results/BENCHMARK.md`](results/BENCHMARK.md).

### Latency — NANOMATCH vs STL Baseline (Realistic Market, 100k resting orders)

| Metric | NANOMATCH | STL (`std::map` + `std::list`) | Speedup |
|---|---|---|---|
| **p50 (median)** | **72.01 ns** | 150.29 ns | **2.08×** |
| **p90** | **73.18 ns** | 174.45 ns | **2.38×** |
| **p99 (tail)** | **75.24 ns** | 212.24 ns | **2.82×** |
| **Std Dev** | ±6.42 ns | ±15.15 ns | — |

> The gap **widens under stress** — the hallmark of a zero-allocation architecture. NANOMATCH's p99 degrades only +3.23 ns above its median. STL degrades +61.95 ns — **19× more**.

### Other Scenario Highlights

| Scenario | p50 | Notes |
|---|---|---|
| 🥇 Order Cancellation | **6.76 ns** | O(1) intrusive list extraction, 0.22% of L1d touched |
| Pure Crossing (L1 ceiling) | **12.51 ns** | Frictionless theoretical lower bound |
| 100% Fill Rate (SPSC stress) | **63.49 ns** | Full logger pipeline, background consumer thread live |
| ⚠️ Pathological Scan (worst case) | **16,754 ns** | O(N) scan across ~19,900 empty slots — disclosed, explained |

### Throughput — mmap Ingestion (5,000,000 orders)

| Mode | Throughput | Notes |
|---|---|---|
| Core engine (logger off) | **23.65M orders/sec** | Pure matching speed |
| Full pipeline (logger on) | **18.17M orders/sec** | Matching + concurrent SPSC trade logger |
| Logger overhead | −23.2% | Cost of `memory_order_release` + cache-line ping-pong |

---

## 🗺️ Navigation Guide

| File | What's Inside | Go Here When… |
|---|---|---|
| [`results/BENCHMARK.md`](results/BENCHMARK.md) | Full percentile tables, speedup analysis, per-scenario deep-dives, working set proofs, integrity methodology, throughput breakdown | You want the complete numbers and the reasoning behind every result |
| [`results/design_internals.md`](results/design_internals.md) | Architectural decisions, data structure design rationale, trade-off analysis | You want to understand *why* the engine is built this way |
| [`results/flamegraph_custom_engine.png`](results/flamegraph_custom_engine.png) | `perf` flame graph — stack ends at `addBuyOrder`/`addSellOrder`, zero `malloc`/`operator new` frames | You want visual proof of the zero-allocation hot path |
| [`results/flamegraph_stl_baseline.png`](results/flamegraph_stl_baseline.png) | `perf` flame graph — 6+ frames deep into heap allocation on every single order | You want to see what STL looks like under `perf` |
| [`results/benchmark_results.json`](results/benchmark_results.json) | Raw Google Benchmark JSON output, all 100 repetitions | You want raw data for independent analysis |
| [`benchmark.cpp`](benchmark.cpp) | Full benchmark suite source — all 6 scenarios, custom p90/p99 statistics | You want to inspect or reproduce the benchmarks |
| [`ingest.cpp`](ingest.cpp) | Zero-copy `mmap` ingestion pipeline, two build targets | You want to see or modify the throughput pipeline |

---

## 📊 Benchmark Report Summary

The full report lives at [`results/BENCHMARK.md`](results/BENCHMARK.md). It covers six scenarios designed to measure a different slice of the engine's performance profile:

- **Order Cancellation** — raw O(1) linked-list extraction speed, proves the pool stays in L1
- **Pure Crossing** — frictionless L1 ceiling; the mathematical lower bound of the engine on this hardware
- **100% Fill Rate** — SPSC ring buffer cost under continuous trade flow with a live consumer thread
- **Realistic Market** ⭐ — the primary result: deep book, cache-missing spray pattern, background logger, 100k resting orders
- **STL Baseline** — identical setup, `std::map` + `std::list`, the apples-to-apples comparison
- **Pathological Scan** — the disclosed worst case: O(N) flat-array scan forced across ~19,900 empty price slots

The report also includes per-scenario working set calculations that prove exactly why the L1 claims hold, a six-point integrity section explaining how each common microbenchmark cheat was defeated, and the full statistical methodology behind the percentile computation.

---

## 🏗️ Architecture & Data Structure Design

The full report lives at [`results/UNDER_THE_HOOD.md`](results/UNDER_THE_HOOD.md)
To achieve median latencies of **~72 nanoseconds**, the data structures were engineered specifically for the CPU's L1/L2 cache, completely bypassing the OS memory manager during runtime.

1. **Intrusive Doubly-Linked List:** Standard `std::list` allocates nodes randomly on the heap, destroying cache locality. In NanoMatch, `next` and `prev` pointers are replaced by integer indices that live directly inside a cache-aligned `Order` struct. This allows O(1) order cancellation without triggering a single heap allocation or unpredictable cache miss.

2. **Pre-Allocated Memory Arena (Object Pool):** `malloc`, `free`, `new`, and `delete` are catastrophic for tail latency — some calls hit the tcmalloc fast path, others fall through to a kernel syscall. NanoMatch pre-allocates a pool of `Order` blocks at startup. Allocating or freeing an order is reduced to an O(1) integer pop/push from a lock-free free-list.

3. **Flat Array Price Levels (Direct Addressing):** Instead of an O(log n) Red-Black tree (`std::map`) to track price levels, a flat statically-sized array is indexed directly by the price tick. Finding the head of a price level is an O(1) array lookup — no pointer chasing, no tree traversal.

---

## 🧠 Hardware Sympathy & Microarchitectural Optimisations

- **Cache-Line Alignment:** Critical structs (`Order` at `alignas(32)`, SPSC atomics at `alignas(64)`) are packed to prevent false sharing and ensure spatial locality when the background logger thread reads memory concurrently.
- **Branch Minimisation:** The crossing logic avoids branching where possible. In the benchmark harness, modulo operators (`%`) were replaced with single-cycle bitwise AND masks (`&`) to prevent the CPU's integer divider from stalling the instruction pipeline.
- **Lock-Free SPSC Ring Buffer:** Trade messages are passed from the matching engine to the logger thread via a single-producer single-consumer ring buffer using `memory_order_release` / `memory_order_acquire` — no mutex, no kernel involvement.
- **Zero-Copy I/O (`mmap`):** Market data ingestion bypasses standard file I/O, using OS-level memory mapping to load multi-million row datasets directly into virtual RAM — achieving **23.65M orders/sec** (logger off) and **18.17M orders/sec** (full pipeline).

---

## 🛡️ Benchmark Integrity

Microbenchmarking is notoriously prone to cheating via unrealistic conditions. Six specific failure modes were identified and explicitly defeated — full detail in [`results/BENCHMARK.md`](results/BENCHMARK.md):

- **No empty-book benchmarking** — `BM_Realistic_Market` pre-loads 100,000 resting orders before the timing loop starts
- **No single price level** — orders spray randomly across a 400-tick window via pre-generated Mersenne Twister distributions, forcing genuine L2/L3 cache pressure
- **No RNG inside the timing loop** — all random data is pre-generated during warmup; the timer measures only the engine
- **No weak baseline** — STL comparison uses `std::map` + `std::list` on the exact same setup: same 100,000 resting orders, same price distribution, same random spray
- **100 repetitions** — not the Google Benchmark default of 1–3; p90 and p99 are meaningful at 100 samples
- **Worst case disclosed** — `BM_Pathological_Scan` deliberately exposes and quantifies the O(N) scan weakness at **16,754 ns p50**

---

## 🛠️ Tech Stack

| Component | Detail |
|---|---|
| **Language** | C++20 |
| **Build System** | CMake + Make (Release forced in `CMakeLists.txt`) |
| **Benchmarking** | Google Benchmark v1.8.3 (auto-fetched) · 100 repetitions · custom p90/p99 statistics |
| **Profiling** | Linux `perf` + Speedscope flame graphs (`-fno-omit-frame-pointer`) |
| **Data Generation** | Python 3 (`generate_orders.py`) |
| **OS** | Linux / WSL2 (POSIX `mmap` required for ingestion pipeline) |
| **Compiler Flags** | `-O3 -march=native -flto` |
| **Threading** | `pthread` · lock-free SPSC ring buffer · `taskset -c 0` core pinning |

---

## 🔨 Build & Run

```bash
# 1. Clone the repository
git clone https://github.com/tscube1130/NANOMATCH.git
cd NANOMATCH

# 2. Generate the synthetic order dataset
python3 generate_orders.py

# 3. Configure and build (Release mode is forced by CMakeLists.txt)
cmake -S . -B build
cmake --build build -j

# 4. Run the latency benchmark suite 
taskset -c 0,1 ./build/nano_bench

# 5. Run the throughput tests
./build/ingest_fast      # Core matching speed only  — ~23.65M orders/sec
./build/ingest_e2e       # Full pipeline + trade logger — ~18.17M orders/sec
```

---

## 🔥 Generating Flame Graphs (Linux perf + Speedscope)

To independently verify the zero-allocation hot path and cache locality, reproduce the flame graphs using Linux `perf` and the Speedscope web visualiser.

**Phase 1 — Record**

```bash
# Record stack traces at 99 Hz
sudo perf record -F 99 -g -- ./build/nano_bench

# Optional: interactive terminal breakdown
sudo perf report

# Export to human-readable text
sudo perf script > profile.txt
```

**Phase 2 — Visualise**

1. Open [speedscope.app](https://www.speedscope.app/) in your browser
2. Drag and drop `profile.txt` into the window
3. Select **Left Heavy** or **Sandwich** view

> Use the **Sandwich** view to confirm the complete absence of `malloc` / `operator new` frames in the NanoMatch stack, and contrast with the STL baseline where heap allocation dominates every call chain.

---

*Every nanosecond is engineered. Every benchmark tells a story.*
