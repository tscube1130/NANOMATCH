# 🧠 Under The Hood — Data Structure Architecture

This document explains *why* each data structure was chosen and exactly what hardware benefit it provides. Every decision traces back to a specific CPU microarchitectural constraint.

---

## The Core Problem: Why STL Fails for HFT

A textbook Limit Order Book uses `std::map<price, std::list<Order>>`. This looks reasonable but creates three compounding performance problems:

**Problem 1 — O(log N) price lookup.** `std::map` is a Red-Black tree. Finding a price level requires traversing ~log₂(N) nodes, each a separately heap-allocated pointer hop. For 10,000 active price levels, that's ~14 pointer chases through random heap memory — almost certainly 14 L2/L3 cache misses.

**Problem 2 — Heap allocation per order.** `std::list::push_back()` calls `malloc` for every single order insertion. On Linux, `malloc` eventually calls `brk()` or `mmap()` — kernel syscalls that trap out of user space, costing hundreds of nanoseconds in the worst case.

**Problem 3 — Non-deterministic tail latency.** `malloc` is fast when the tcmalloc free-list has a slot (O(1)), but slow when it must request a new page from the kernel (O(syscall)). This makes p99/p999 latency unpredictable. The flame graph at `results/flamegraph_stl_baseline.png` shows this exactly: `malloc` appears as a major frame in the STL hot path.

---

## Solution 1: Flat Array Price Levels — O(1) Lookup

```cpp
static constexpr size_t MAX_PRICE_TICKS = 200000;
std::vector<PriceLevel> bids;
std::vector<PriceLevel> asks;
```

Instead of a tree, price is used **directly as the array index**. `bids[10050]` is the price level for tick 10050 — no traversal, no pointer chase, just a base pointer offset calculated in one CPU cycle.

Both vectors are allocated once at construction time via `resize(MAX_PRICE_TICKS)`, so there is no per-operation heap activity. Choosing `std::vector` over `std::array` keeps the 6.4 MB allocation on the heap rather than the stack, which avoids stack-overflow risk for large `MAX_PRICE_TICKS` values while preserving the same contiguous, stride-1 memory layout.

**Trade-off:** The arrays occupy memory proportional to `MAX_PRICE_TICKS`. With `PriceLevel` at 16 bytes, two arrays = 2 × 200,000 × 16 = 6.4 MB. This fits in L3 cache (16 MB on our test machine), so even cold price-level reads hit L3 rather than DRAM.

**The known weakness:** When a price level empties, the engine must linearly scan to find the next active level (`bestBidPrice` / `bestAskPrice`). In liquid markets this scan is 1–10 ticks. In the pathological case, it can reach O(N). This trade-off is explicitly measured and disclosed in the `BM_Pathological_Scan` benchmark. See [`1_LATENCY_PROFILE.md`](1_LATENCY_PROFILE.md) Section 6.

---

## Solution 2: Pre-Allocated Order Pool — Zero Runtime Allocation

```cpp
class OrderPool {
    std::vector<Order> pool;   // Contiguous block, allocated once at startup
    int32_t free_head = 0;     // Head of the embedded free list
    ...
    inline int32_t allocateOrder() noexcept {
        if (free_head == -1) return -1;
        int32_t index = free_head;
        free_head = pool[index].nextOrderIndex; // O(1) array read, no syscall
        return index;
    }
    inline void freeOrder(int32_t index) noexcept {
        pool[index].nextOrderIndex = free_head;
        free_head = index;
    }
};
```

At startup, `OrderPool(1,000,000)` allocates a single contiguous block of 1M `Order` structs and links every idle slot through its own `nextOrderIndex` field — forming an **embedded free list** threaded directly through the pool array. No separate bookkeeping vector is needed: a single `int32_t free_head` tracks the entire list.

During operation, `allocateOrder()` reads `free_head`, follows one array element to get the next free slot, and updates `free_head` — two integer reads and one write. `freeOrder()` prepends the returned index back onto the list with two integer writes. Neither operation calls `malloc`, `free`, or any OS primitive.

**Hardware benefit:** Because all `Order` objects live in one contiguous block, sequential access patterns are prefetcher-friendly. The CPU's hardware prefetcher detects the stride and pre-loads adjacent cache lines before they are needed.

**Proof:** `results/flamegraph_custom_engine.png` shows the hot path ending at `addBuyOrder` / `addSellOrder` with no `malloc` or `operator new` frames anywhere in the stack.

---

## Solution 3: Intrusive Doubly-Linked List — O(1) Cancellation

Standard `std::list` nodes are allocated on the heap — cancelling an order requires finding the node (via a separate map lookup), then calling `std::list::erase()`, which calls `free()`.

NANOMATCH embeds the list linkage directly inside the `Order` struct:

```cpp
struct alignas(32) Order {
    uint64_t orderId;
    uint64_t price;
    uint32_t quantity;
    Side side;
    int32_t nextOrderIndex = -1;   // Index into OrderPool, not a pointer
    int32_t prevOrderIndex = -1;   // Index into OrderPool, not a pointer
};
```

The `nextOrderIndex` and `prevOrderIndex` fields are **integer indices into the pool array**, not raw pointers. This means:

1. No heap allocation per node — the struct already lives in the pool
2. Cancellation (`cancelOrder`) just rewires two integers: `pool[prev].next = node.next` and `pool[next].prev = node.prev`. No `free()`. No syscall. Pure integer stores.
3. `orderMap[orderId]` gives the pool index in O(1) via **direct array indexing** — `orderMap` is a `std::vector<int32_t>` pre-sized to the maximum order ID space, so lookup is a single array read with no hashing overhead whatsoever.

**Result:** `BM_Order_Cancellation` measures **6.76 ns p50** — the entire insert + cancel round trip.

---

## Solution 4: Cache-Line Alignment

### Order struct — `alignas(32)`

```cpp
struct alignas(32) Order { ... }; // 32 bytes exactly
```

At 32 bytes, exactly **two `Order` objects fit within one 64-byte L1 cache line**. Fetching any `Order` from the pool also loads its neighbour into the cache line — useful when iterating through a price level's queue (adjacent orders in the pool tend to be inserted together).

### SPSC Ring Buffer — `alignas(64)` on head and tail

```cpp
alignas(64) std::atomic<size_t> head{0};  // Cache line 1 — owned by producer
alignas(64) std::atomic<size_t> tail{0};  // Cache line 2 — owned by consumer
```

Without this alignment, `head` and `tail` could share a 64-byte cache line. When the producer writes `head` and the consumer writes `tail`, both cores would repeatedly invalidate each other's cache line — **false sharing**, costing ~40–100 ns per operation on a multi-core system.

With `alignas(64)`, the producer owns cache line 1 exclusively and the consumer owns cache line 2 exclusively. No cross-core cache invalidation.

---

## Solution 5: SPSC Ring Buffer — Lock-Free Trade Logging

```cpp
template<size_t Capacity>
class SPSCRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0); // Power of 2 required
    ...
    inline bool push(const TradeMsg& msg) noexcept {
        size_t next_head = (current_head + 1) % Capacity;
        // Note: % is correct for non-power-of-2 safe fallback.
        // With power-of-2 Capacity this compiles to a bitwise AND.
        ...
    }
};
```

The ring buffer uses `std::memory_order_acquire` / `std::memory_order_release` — the minimum necessary memory ordering for SPSC correctness. We do not use `seq_cst` (the default for `std::atomic`), which would insert a full memory fence (`MFENCE` instruction) on x86, costing ~40–60 CPU cycles.

**Producer:** `head` is read with `relaxed` (producer owns it), `tail` is read with `acquire` (must see consumer's latest commit). `head` is written with `release` (makes message visible to consumer).

**Consumer:** `tail` is read with `relaxed` (consumer owns it), `head` is read with `acquire` (must see producer's latest push). `tail` is written with `release`.

This is the minimal safe ordering for SPSC — any weaker and message visibility is not guaranteed across cores.

---

## The `bestBidPrice` / `bestAskPrice` Cursor

Two scalar values track the current best bid and ask prices:

```cpp
uint64_t bestBidPrice = 0;
uint64_t bestAskPrice = MAX_PRICE_TICKS;
```

These eliminate the need to search the array from scratch on every match. The matching loop starts at `bestAskPrice` and steps forward only when a level empties. In the common case (liquid market, spread of 1–5 ticks), this scan is essentially free.

**Important nuance for cancellations:** When `cancelOrder()` removes the last order at the current best price, `bestBidPrice` / `bestAskPrice` is *not* updated immediately in all code paths. The matching loop will skip the now-empty level on the next iteration. This is a deliberate choice — updating the cursor on every cancel would require a scan, adding latency to the cancel path. The matching loop handles the skip gracefully with the `[[unlikely]]` empty-level check.