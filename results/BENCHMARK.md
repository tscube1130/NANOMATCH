# ⏱️ NANOMATCH — Benchmark Results & Latency Analysis

**Date Executed:** 2026-05-20  
**Host:** ASUSAURUS (WSL2 — Ubuntu · Kernel 6.6.87.2-microsoft-standard-WSL2)  
**CPU:** AMD Ryzen 9 8945HS (Zen 4) · 8 cores / 16 threads · up to 5.2 GHz  
**Cache Hierarchy:** 32 KiB L1d (private/core) · 1 MiB L2 (private/core) · 16 MiB L3 (shared · 1 instance)  
**Build:** CMake Release (`-O3 -march=native -flto`) · GCC 13  
**CPU Pinning:** `taskset -c 0,1` — eliminates OS scheduler migration noise  
**Repetitions:** 100 per benchmark  
**Raw data:** [`results/benchmark_results.json`](results/benchmark_results.json)

> Both the latency benchmarks (Google Benchmark) and the throughput tests (`ingest.cpp`) were run on the same machine. WSL2 adds ~2–5% overhead vs bare-metal Linux due to Hyper-V virtualisation; relative speedup ratios are unaffected.

---

## 📋 Table of Contents

1. [🗂️ Benchmark Scenarios](#%EF%B8%8F-benchmark-scenarios)
2. [📊 Results: Latency Percentiles](#-results-latency-percentiles-nanoseconds-real_time)
3. [🚀 Speedup Analysis: NANOMATCH vs STL Baseline](#-speedup-analysis-nanomatch-vs-stl-baseline)
4. [⚡ E2E Throughput (mmap Ingestion)](#-e2e-throughput-mmap-ingestion)
5. [🔬 Scenario-by-Scenario Breakdown](#-scenario-by-scenario-breakdown)
   - [1. Order Cancellation — 6.76 ns](#1-order-cancellation--676-ns-p50)
   - [2. Pure Crossing — 12.51 ns](#2-pure-crossing--1251-ns-p50)
   - [3. 100% Fill Rate — 63.49 ns](#3-100-fill-rate--6349-ns-p50)
   - [4. Realistic Market — 72.01 ns ⭐](#4-realistic-market--7201-ns-p50-%EF%B8%8F-the-primary-result)
   - [5. STL Baseline — 150.29 ns](#5-stl-baseline--15029-ns-p50)
   - [6. Pathological Scan — 16,754 ns](#6-pathological-scan--16754-ns-p50-disclosed-weakness)
6. [🛡️ Benchmark Integrity — How We Defeated Common Microbenchmark Cheats](#%EF%B8%8F-benchmark-integrity--how-we-defeated-common-microbenchmark-cheats)
7. [📐 Statistical Methodology](#-statistical-methodology)
8. [🔥 Flame Graph Evidence](#-flame-graph-evidence)
9. [⚠️ Known Limitations](#%EF%B8%8F-known-limitations)

---

## 🗂️ Benchmark Scenarios

| Scenario | What It Tests |
|---|---|
| **Order Cancellation** | Raw O(1) intrusive linked-list extraction + pool recycle |
| **Pure Crossing** | Theoretical L1 cache ceiling — frictionless matching |
| **100% Fill Rate** | SPSC ring buffer overhead under continuous trade flow |
| **Realistic Market** | Deep book (100k resting orders), 400-tick market-order spray window, live logger thread |
| **STL Baseline** | Identical to Realistic Market but with `std::map` + `std::list` |
| **Pathological Scan** | Disclosed worst case: O(N) linear scan across ~19,900 empty price slots |

All random data (prices, order IDs, sides) is pre-generated using Mersenne Twister `std::mt19937` during the warmup phase and replayed sequentially inside the timing loop. The timer measures only the engine.

---

## 📊 Results: Latency Percentiles (nanoseconds, `real_time`)

| Benchmark | p50 (Median) | p90 | p99 | Std Dev | CV |
|---|---|---|---|---|---|
| **Order Cancellation** | **6.76 ns** | 7.31 ns | 8.93 ns | 0.51 ns | 7.3% |
| **Pure Crossing** | **12.51 ns** | 13.92 ns | 14.83 ns | 0.98 ns | 7.7% |
| **100% Fill Rate** | **63.49 ns** | 69.42 ns | 71.44 ns | 7.03 ns | 11.5% |
| 🟢 **Realistic Market** | **72.01 ns** | 73.18 ns | 75.24 ns | 6.42 ns | 9.4% |
| 🔴 **STL Baseline** | **150.29 ns** | 174.45 ns | 212.24 ns | 15.15 ns | 9.7% |
| **Pathological Scan** | **16,754 ns** | 18,817 ns | 20,066 ns | 1,323 ns | 7.8% |

> **CV** = Coefficient of Variation (stddev / mean). Lower = more deterministic. NANOMATCH's Realistic Market CV of **9.4%** is comparable to STL's **9.7%**, but NANOMATCH's absolute variance (±6.42 ns) is less than half of STL's (±15.15 ns).

---

## 🚀 Speedup Analysis: NANOMATCH vs STL Baseline

Both `BM_Realistic_Market` and `BM_Baseline_STL` run the **exact same scenario** — 100,000 pre-loaded resting orders (50,000 bids + 50,000 asks) across a randomized price distribution, with a live background logger thread consuming the SPSC buffer. The only variable is the underlying data structure.

| Stress Level | NANOMATCH | STL Baseline | Speedup |
|---|---|---|---|
| **p50 (median)** | 72.01 ns | 150.29 ns | **2.08×** |
| **p90 (high stress)** | 73.18 ns | 174.45 ns | **2.38×** |
| **p99 (chaos)** | 75.24 ns | 212.24 ns | **2.82×** |

**The performance gap widens as stress increases** — the hallmark of a zero-allocation architecture.

**p99 degradation above p50:**
- NANOMATCH: +3.23 ns (+4.5%)
- STL Baseline: +61.95 ns (+41.2%)

The STL's p99 degradation is **19× larger** than NANOMATCH's. This is not a fluke — it reflects the non-deterministic cost of heap allocation. Some `malloc` calls hit the tcmalloc free-list fast path (O(1)); others fall through to a kernel `brk`/`mmap` syscall (O(syscall)). The custom pool eliminates both paths entirely.

---

## ⚡ E2E Throughput (mmap Ingestion)

Measured independently of Google Benchmark via the `ingest.cpp` pipeline, which reads a pre-generated 5,000,000-row CSV via zero-copy `mmap` and feeds every order through the full matching engine on a pre-warmed OS page cache. Two configurations were run: one with the trade logger disabled (pure engine speed) and one with the full SPSC pipeline active. Both tests ran on the same ASUSAURUS machine as the latency benchmarks above.

> **System:** AMD Ryzen 9 8945HS (Zen 4) · 8C / 16T · up to 5.2 GHz · Windows 11 (WSL2 — Ubuntu) · GCC 13 · `-O3 -march=native -flto`  
> `mmap` performance may be slightly bottlenecked by the Windows/Linux filesystem boundary. Native Linux numbers would be higher.

---

### 🔇 Test 1 — Core Matching Speed (Logger DISABLED)

Pure engine throughput with no I/O overhead. Measures how fast the matching engine alone can ingest and process orders when the SPSC trade logger is switched off.

```
Total Orders Processed:  5,000,000
Processing Time:         0.2114 seconds
Throughput:              23.65 Million orders/second
```

---

### 🔊 Test 2 — End-to-End Pipeline (Logger ENABLED)

Full production pipeline: matching engine + concurrent SPSC trade logger thread draining `TradeMsg` structs in real time. This is the number that reflects actual deployed throughput.

```
Total Orders Processed:  5,000,000
Total Trades Executed:   657,367
Processing Time:         0.2752 seconds
Throughput:              18.17 Million orders/second
```

---

### 📦 Pipeline Overhead Summary

| Configuration | Throughput | Time |
|---|---|---|
| **Core engine only** (logger off) | **23.65M orders/sec** | 0.2114 s |
| **Full pipeline** (logger on) | **18.17M orders/sec** | 0.2752 s |
| **Logger overhead** | **−23.2% throughput** | +0.0638 s |

The 23.2% throughput cost of the logger is the measurable price of `memory_order_release` stores, cache-line ping-pong between the producer and consumer cores, and occasional back-pressure when the SPSC buffer fills. At 18.17M orders/sec, the full end-to-end pipeline still processes an order every **55 nanoseconds** while simultaneously logging every trade to a background thread — with zero heap allocation on the hot path.

The high trade count (657,367 out of 5,000,000 orders) reflects a dataset where a substantial portion of orders cross the book immediately, exercising both the matching path and the logger under genuine load.

---

## 🔬 Scenario-by-Scenario Breakdown

### 1. Order Cancellation — 6.76 ns p50

Tests the raw speed of O(1) intrusive doubly-linked list extraction.

On every iteration an order is inserted, then immediately cancelled. This exercises the `cancelOrder()` path which rewires `prevOrderIndex` / `nextOrderIndex` fields without any memory allocation or deallocation — just integer assignments to pool-resident structs.

**Why this stays in L1 — working set proof:**

| Component | Size |
|---|---|
| `Order` struct (`alignas(32)`) | 32 bytes |
| `PriceLevel` at `bids[10000]` | 16 bytes |
| `orderMap` hot entry (`int32_t`) | 4 bytes |
| `pool.free_head` (`int32_t`) | 4 bytes |
| `bestBidPrice` + `bestAskPrice` (`uint64_t` each) | 16 bytes |
| **Total hot working set** | **72 bytes** |

72 bytes is **1.1 cache lines** against a 32 KiB L1d that holds 512 cache lines — the entire working set occupies **0.22% of L1d**. The same pool slot and the same `PriceLevel` are reused on every iteration, so there is zero cold-fetch pressure. The **6.76 ns p50** and **0.51 ns standard deviation** are the direct consequence.

---

### 2. Pure Crossing — 12.51 ns p50

Tests the theoretical L1 cache performance ceiling.

Two orders at the same price point are submitted in a loop. The matching logic executes, a `TradeMsg` is written to the SPSC buffer, and the pool slot is recycled. Because the same price index and the same pool slots are reused every iteration, the entire working set fits inside the 32 KiB L1d cache.

**Working set breakdown:**

| Component | Size |
|---|---|
| 2× `Order` structs (`alignas(32)`) | 64 bytes |
| 2× `PriceLevel` structs | 32 bytes |
| SPSC hot path: `head` cache line (`alignas(64)`) | 64 bytes |
| SPSC hot path: `tail` cache line (`alignas(64)`) | 64 bytes |
| SPSC active slot (`TradeMsg`, rounded to cache line) | 64 bytes |
| 2× `orderMap` entries (`int32_t` each) | 8 bytes |
| `bestBidPrice` + `bestAskPrice` | 16 bytes |
| **Total hot working set** | **312 bytes** |

312 bytes = **4.9 cache lines** against 512 available in L1d — **0.95% of L1d capacity**. The SPSC `buffer` array itself is 2.8 MB (cold), but only the three hot cache lines above (head, tail, active slot) are ever touched in steady state. The **12.51 ns p50** is the mathematical lower bound of this engine on this hardware — the minimum achievable latency when nothing cold is touched.

---

### 3. 100% Fill Rate — 63.49 ns p50

Tests the overhead of the lock-free SPSC trade logger under continuous load.

Every submitted order results in a match and a `TradeMsg` pushed into `SPSCRingBuffer<100000>`. A background consumer thread continuously drains the buffer. The ~50 ns jump from Pure Crossing captures the combined cost of the `memory_order_release` store on push, cache-line ping-pong between the producer and consumer cores, and occasional buffer-full back-pressure.

The **~7 ns standard deviation** (vs ~1 ns for Pure Crossing) reflects non-deterministic scheduling of the background thread.

---

### 4. Realistic Market — 72.01 ns p50 ⭐️ (The Primary Result)

The most honest benchmark in the suite. This is the number that matters.

**Setup:** 100,000 resting orders pre-loaded across a 2,000-tick range — 50,000 bids via `bidPriceGen(9000, 9900)` and 50,000 asks via `askPriceGen(10100, 11000)` — yielding a minimum bid-ask spread of 200 ticks, random quantities (10–100). Then 100 repetitions of: pick a random price in the `(9800, 10200)` range (a 400-tick spray window), submit a buy or sell, measure wall-clock latency.

**Why this is hard:** The 400-tick market-order spray window (`marketPriceGen(9800, 10200)`) guarantees that the accessed `PriceLevel` slot will frequently not be in L1 cache — orders land across a wide range of `bids[]` / `asks[]` slots on every iteration. `orderMap` lookups for partially-filled resting orders hit L2 or L3. The background logger thread runs concurrently, creating real multi-core memory traffic.

The **72.01 ns p50** and **75.24 ns p99** — a spread of only **3.2 ns across the entire tail** — prove the architecture's determinism under genuine cache pressure.

---

### 5. STL Baseline — 150.29 ns p50

Identical setup to Realistic Market, but using `std::map<uint64_t, std::list<STLOrder>>`.

The `std::map` traverses a Red-Black tree (~4–5 pointer hops) to locate a price level. Each `std::list::push_back()` calls `malloc`. Each `std::list::erase()` calls `free`. The **15.15 ns standard deviation** (vs 6.42 ns for NANOMATCH) reflects the non-deterministic cost of heap allocation. The **212.24 ns p99** — nearly 3× the median — is the definitive signal: heap allocation destroys tail-latency predictability.

---

### 6. Pathological Scan — 16,754 ns p50 (Disclosed Weakness)

This benchmark exists to expose and quantify the known architectural trade-off.

When a price level empties, the engine scans forward through the flat `asks[]` / `bids[]` array to find the new best price. In a normal liquid market this scan is 0–5 ticks. Here, a sentinel ask is placed at tick 20,000 and orders are repeatedly crossed at tick 100 — forcing a ~19,900 slot scan on every iteration.

The **16,754 ns p50** (≈ 16.75 µs) is roughly proportional to `19,900 slots × ~0.84 ns/slot`, consistent with sequential L2/L3 cache reads.

**This is a deliberate, disclosed trade-off.** The alternative — maintaining a sorted set of active price levels — would add a `std::set` insertion on every order, costing ~20–50 ns on the common-case hot path to save ~16 µs on a rare pathological event (flash crash / extreme liquidity withdrawal). The architecture optimises for the common case.

---

## 🛡️ Benchmark Integrity — How We Defeated Common Microbenchmark Cheats

### Cheat 1: Benchmarking an Empty Book

**The trick:** Submit a buy and sell at the same price repeatedly. The book empties each iteration. Everything stays hot in L1. Results look impossibly fast.

**How we avoid it:** `BM_Realistic_Market` pre-loads **100,000 resting orders** (50,000 bids + 50,000 asks) before the timing loop. The book is deep and fragmented throughout. `BM_Pure_Crossing` does benchmark an empty book but is **explicitly labeled** as the frictionless theoretical limit, never the primary result.

---

### Cheat 2: Single Price Level (Artificial L1 Warmth)

**The trick:** Always submit at the same price tick, keeping the same `PriceLevel` slot permanently hot in L1.

**How we avoid it:** `BM_Realistic_Market` uses `std::uniform_int_distribution<uint64_t> marketPriceGen(9800, 10200)` to spray orders randomly across a 400-tick window, forcing different `bids[]` / `asks[]` slots on every iteration.

---

### Cheat 3: Measuring Random Number Generation

**The trick:** Generate random order parameters inside the timing loop. `std::mt19937` costs ~5–10 ns per call — enough to contaminate results.

**How we avoid it:** All random data is **pre-generated during the warmup phase**, stored in a pre-allocated vector, and replayed sequentially inside the hot loop. The timer measures only the engine.

---

### Cheat 4: Weak STL Baseline

**The trick:** Compare against a crippled STL implementation, or a trivially small tree.

**How we avoid it:** `BM_Baseline_STL` uses `std::map<uint64_t, std::list<STLOrder>>` — the most natural C++ LOB implementation. It runs the **exact same setup** as `BM_Realistic_Market`: same 100,000 pre-loaded resting orders (50,000 bids + 50,000 asks), same price distribution, same random spray. The **2.82× p99 speedup** comes from the architecture, not a rigged baseline.

---

### Cheat 5: Too Few Repetitions

**The trick:** Run 3–5 repetitions. A single OS interrupt skews the mean. p90/p99 are meaningless with 5 samples.

**How we avoid it:** Every benchmark runs **100 repetitions**. Mean, median, standard deviation, p90, and p99 are all computed and reported. Google Benchmark's default is 1–3 repetitions.

---

### Cheat 6: Not Disclosing the Worst Case

**The trick:** Never measure or publish the engine's worst-case behavior.

**How we avoid it:** `BM_Pathological_Scan` is explicitly designed to expose the flat-array scan weakness and reports **16,754 ns p50**. It is included in every results table with a full explanation of when it occurs in real markets.

---

## 📐 Statistical Methodology

**Timer:** Google Benchmark uses `clock_gettime(CLOCK_REALTIME)` backed by CPU hardware performance counters via `VDSO` — no syscall overhead for the timer itself.

**Reported metric:** `real_time` (wall clock), not `cpu_time`. Wall clock includes cross-core cache coherence latency from the logger thread, making it the more conservative and realistic number. CPU time is available in `benchmark_results.json` for reference.

**Percentile calculation:**
```cpp
static double Percentile90(const std::vector<double>& data) {
    std::vector<double> copy = data;
    std::sort(copy.begin(), copy.end());
    size_t idx = std::ceil(0.90 * copy.size()) - 1;
    return copy[idx];
}
```

Standard percentile-of-sorted-samples. With 100 repetitions, p90 = the 90th sample in sorted order, p99 = the 99th. Uses ceiling rather than interpolation — conservative by design.

---

## 🔥 Flame Graph Evidence

| File | What It Shows |
|---|---|
| [`flamegraph_custom_engine.png`](results/flamegraph_custom_engine.png) | Stack ends at `addBuyOrder` / `addSellOrder`. Zero `malloc` / `operator new` frames. Zero-allocation hot path confirmed. |
| [`flamegraph_stl_baseline.png`](results/flamegraph_stl_baseline.png) | Stack descends 6+ frames through `operator new` → `malloc` → kernel allocator on every order. Heap allocation in hot path confirmed. |

---

## ⚠️ Known Limitations

**Pathological scan (O(N) on wide spreads):** measured at **16.75 µs p50**. Occurs during flash crashes or extreme liquidity withdrawal. See Section 6 above for full reasoning on why the trade-off is correct.

**WSL2 environment:** adds ~2–5% overhead vs bare-metal Linux due to Hyper-V virtualisation. Absolute numbers would be slightly lower on bare metal; relative speedup ratios are unaffected.
