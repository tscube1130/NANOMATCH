#include <iostream>
#include <cstdint>
#include <vector>
#include <array>
#include <atomic>
#include <memory>
#include <thread>
#include <random>
#ifndef INGEST_BUILD
#include <benchmark/benchmark.h>
#endif
#include <algorithm>
#include <cmath>
#include<list>
#include<unordered_map>
#include<map>

using namespace std;

// ==========================================
// BASELINE FOR COMPARISON: NAIVE STL ENGINE
// ==========================================
struct STLOrder {
    uint64_t orderId;
    uint64_t price;
    uint32_t quantity;
};

class LimitOrderBookSTL {
private:
    // std::map is a Red-Black Tree. It auto-sorts, but allocates nodes randomly in RAM!
    std::map<uint64_t, std::list<STLOrder>, std::greater<uint64_t>> bids;
    std::map<uint64_t, std::list<STLOrder>> asks;
    std::unordered_map<uint64_t, std::list<STLOrder>::iterator> orderMap;

public:
    void addBuyOrder(uint64_t orderId, uint64_t price, uint32_t quantity) {
        auto it = asks.begin();
        while (quantity > 0 && it != asks.end() && it->first <= price) {
            auto& list = it->second;
            auto listIt = list.begin();
            while (quantity > 0 && listIt != list.end()) {
                uint64_t tradeQty = std::min(static_cast<uint64_t>(quantity), static_cast<uint64_t>(listIt->quantity));
                quantity -= tradeQty;
                listIt->quantity -= tradeQty;

                if (listIt->quantity == 0) {
                    orderMap.erase(listIt->orderId);
                    listIt = list.erase(listIt);
                } else {
                    ++listIt;
                }
            }
            if (list.empty()) {
                it = asks.erase(it);
            } else {
                ++it;
            }
        }
        if (quantity > 0) {
            bids[price].push_back({orderId, price, quantity});
            orderMap[orderId] = std::prev(bids[price].end());
        }
    }

    void addSellOrder(uint64_t orderId, uint64_t price, uint32_t quantity) {
        auto it = bids.begin();
        while (quantity > 0 && it != bids.end() && it->first >= price) {
            auto& list = it->second;
            auto listIt = list.begin();
            while (quantity > 0 && listIt != list.end()) {
                uint64_t tradeQty = std::min(static_cast<uint64_t>(quantity), static_cast<uint64_t>(listIt->quantity));
                quantity -= tradeQty;
                listIt->quantity -= tradeQty;

                if (listIt->quantity == 0) {
                    orderMap.erase(listIt->orderId);
                    listIt = list.erase(listIt);
                } else {
                    ++listIt;
                }
            }
            if (list.empty()) {
                it = bids.erase(it);
            } else {
                ++it;
            }
        }
        if (quantity > 0) {
            asks[price].push_back({orderId, price, quantity});
            orderMap[orderId] = std::prev(asks[price].end());
        }
    }
};

// ==========================================
// 1. DATA STRUCTURES & BUFFERS
// ==========================================

enum class Side: uint8_t { BUY, SELL };

struct TradeMsg {
    uint64_t buyerOrderId;
    uint64_t sellerOrderId;
    uint64_t price;
    uint32_t quantity;
};

struct alignas(32) Order {
    uint64_t orderId;        
    uint64_t price;          
    uint32_t quantity;       
    Side side;               
    int32_t nextOrderIndex = -1; 
    int32_t prevOrderIndex = -1; 
};

struct PriceLevel {
    int32_t headOrderIndex = -1; 
    int32_t tailOrderIndex = -1; 
    uint64_t totalVolume = 0;    
};

template<size_t Capacity>
class SPSCRingBuffer {
private:
    std::array<TradeMsg, Capacity> buffer;
    alignas(64) std::atomic<size_t> head{0}; 
    alignas(64) std::atomic<size_t> tail{0}; 

public:
    inline bool push(const TradeMsg& msg) noexcept {
        size_t current_head = head.load(std::memory_order_relaxed);
        size_t next_head = (current_head + 1) % Capacity;
        if (next_head == tail.load(std::memory_order_acquire)) return false; 

        buffer[current_head] = msg;
        head.store(next_head, std::memory_order_release);
        return true;
    }

    inline bool pop(TradeMsg& msg) noexcept {
        size_t current_tail = tail.load(std::memory_order_relaxed);
        if (current_tail == head.load(std::memory_order_acquire)) return false; 

        msg = buffer[current_tail];
        tail.store((current_tail + 1) % Capacity, std::memory_order_release);
        return true;
    }
};

class OrderPool {
private:
    std::vector<Order> pool;       
    std::vector<int32_t> freeList; 

public:
    OrderPool(size_t capacity) {
        pool.resize(capacity); 
        freeList.reserve(capacity);
        for (int32_t i = capacity - 1; i >= 0; --i) {
            freeList.push_back(i);
        }
    }

    inline int32_t allocateOrder() noexcept {
        if (freeList.empty()) return -1; 
        int32_t index = freeList.back();
        freeList.pop_back();
        return index;
    }

    inline void freeOrder(int32_t index) noexcept {
        freeList.push_back(index);
    }

    inline Order& getOrder(int32_t index) noexcept { return pool[index]; }
};

// ==========================================
// 2. THE ENGINE (WITH STRUCTURAL FIXES)
// ==========================================

class LimitOrderBook {
private:
    static constexpr size_t MAX_PRICE_TICKS = 200000;
    std::array<PriceLevel, MAX_PRICE_TICKS> bids; 
    std::array<PriceLevel, MAX_PRICE_TICKS> asks; 
    std::vector<int32_t> orderMap;

    OrderPool pool;
    SPSCRingBuffer<100000>* tradeBuffer;

    uint64_t bestBidPrice = 0;
    uint64_t bestAskPrice = MAX_PRICE_TICKS; 

public:
    LimitOrderBook(size_t maxOrders, SPSCRingBuffer<100000>* buffer) : pool(maxOrders), tradeBuffer(buffer) {
    orderMap.resize(maxOrders + 1, -1); // +1 because Order IDs start at 1

    }
    
    inline void addBuyOrder(uint64_t orderId, uint64_t price, uint32_t quantity) noexcept {
        while (quantity > 0 && price >= bestAskPrice) {
            PriceLevel& bestAskLevel = asks[bestAskPrice];
            if (bestAskLevel.headOrderIndex == -1) [[unlikely]] {
                bestAskPrice++;
                continue;
            }

            int32_t currentAskIndex = bestAskLevel.headOrderIndex;
            Order& restingAsk = pool.getOrder(currentAskIndex);

            uint64_t tradeQty = (quantity < restingAsk.quantity) ? quantity : restingAsk.quantity;
            quantity -= tradeQty;
            restingAsk.quantity -= tradeQty;
            bestAskLevel.totalVolume -= tradeQty;

            if (tradeQty > 0 && tradeBuffer) {
                tradeBuffer->push({orderId, restingAsk.orderId, bestAskPrice, static_cast<uint32_t>(tradeQty)});
            }

            if (restingAsk.quantity == 0) [[likely]] {
                bestAskLevel.headOrderIndex = restingAsk.nextOrderIndex;
                if (bestAskLevel.headOrderIndex == -1) [[unlikely]] bestAskLevel.tailOrderIndex = -1;
                orderMap[restingAsk.orderId] = -1;
                pool.freeOrder(currentAskIndex);
            }

            if (bestAskLevel.headOrderIndex == -1) {
                do { bestAskPrice++; } while (bestAskPrice < MAX_PRICE_TICKS && asks[bestAskPrice].headOrderIndex == -1);
            }
        }

        if (quantity > 0) [[likely]] {
            int32_t newOrderIndex = pool.allocateOrder();
            if (newOrderIndex == -1) [[unlikely]] return; 
            orderMap[orderId] = newOrderIndex;

            Order& newOrder = pool.getOrder(newOrderIndex);
            newOrder.orderId = orderId;
            newOrder.price = price;
            newOrder.quantity = quantity;
            newOrder.side = Side::BUY;
            newOrder.nextOrderIndex = -1; 

            PriceLevel& bidLevel = bids[price];
            if (bidLevel.tailOrderIndex == -1) [[unlikely]] {
                bidLevel.headOrderIndex = newOrderIndex;
            } else [[likely]] {
                Order& oldTail = pool.getOrder(bidLevel.tailOrderIndex);
                oldTail.nextOrderIndex = newOrderIndex;
                newOrder.prevOrderIndex = bidLevel.tailOrderIndex; 
            }
            
            bidLevel.tailOrderIndex = newOrderIndex;
            bidLevel.totalVolume += quantity;

            if (price > bestBidPrice) bestBidPrice = price;
        }
    }

    inline void addSellOrder(uint64_t orderId, uint64_t price, uint32_t quantity) noexcept {
        while (quantity > 0 && price <= bestBidPrice) {
            PriceLevel& bestBidLevel = bids[bestBidPrice];
            if (bestBidLevel.headOrderIndex == -1) [[unlikely]] {
                if (bestBidPrice == 0) break; 
                bestBidPrice--;
                continue;
            }

            int32_t currentBidIndex = bestBidLevel.headOrderIndex;
            Order& restingBid = pool.getOrder(currentBidIndex);

            uint64_t tradeQty = (quantity < restingBid.quantity) ? quantity : restingBid.quantity;
            quantity -= tradeQty;
            restingBid.quantity -= tradeQty;
            bestBidLevel.totalVolume -= tradeQty;

            if (tradeQty > 0 && tradeBuffer) {
                tradeBuffer->push({restingBid.orderId, orderId, bestBidPrice, static_cast<uint32_t>(tradeQty)});
            }

            if (restingBid.quantity == 0) [[likely]] {
                bestBidLevel.headOrderIndex = restingBid.nextOrderIndex;
                if (bestBidLevel.headOrderIndex == -1) [[unlikely]] bestBidLevel.tailOrderIndex = -1;
                orderMap[restingBid.orderId] = -1;
                pool.freeOrder(currentBidIndex);
            }

            if (bestBidLevel.headOrderIndex == -1) {
                while (bestBidPrice > 0 && bids[bestBidPrice].headOrderIndex == -1) bestBidPrice--;
            }
        }

        if (quantity > 0) [[likely]] {
            int32_t newOrderIndex = pool.allocateOrder();
            // FIX 1: Check for exhaustion BEFORE writing to map
            if (newOrderIndex == -1) [[unlikely]] return; 
            orderMap[orderId] = newOrderIndex;

            Order& newOrder = pool.getOrder(newOrderIndex);
            newOrder.orderId = orderId;
            newOrder.price = price;
            newOrder.quantity = quantity;
            newOrder.side = Side::SELL;
            newOrder.nextOrderIndex = -1;

            PriceLevel& askLevel = asks[price];
            if (askLevel.tailOrderIndex == -1) [[unlikely]] {
                askLevel.headOrderIndex = newOrderIndex;
            } else [[likely]] {
                Order& oldTail = pool.getOrder(askLevel.tailOrderIndex);
                oldTail.nextOrderIndex = newOrderIndex;
                // FIX 2: Set the backward pointer!
                newOrder.prevOrderIndex = askLevel.tailOrderIndex;
            }
            
            askLevel.tailOrderIndex = newOrderIndex;
            askLevel.totalVolume += quantity;

            if (price < bestAskPrice) bestAskPrice = price;
        }
    }

    inline void cancelOrder(uint64_t orderId) noexcept {
        int32_t orderIndex = orderMap[orderId];
        if (orderIndex == -1) [[unlikely]] return; 

        Order& order = pool.getOrder(orderIndex);
        PriceLevel& level = (order.side == Side::BUY) ? bids[order.price] : asks[order.price];

        // 1. Remove from Doubly-Linked List (O(1) time)
        if (order.prevOrderIndex != -1) {
            pool.getOrder(order.prevOrderIndex).nextOrderIndex = order.nextOrderIndex;
        } else {
            level.headOrderIndex = order.nextOrderIndex; 
        }

        if (order.nextOrderIndex != -1) {
            pool.getOrder(order.nextOrderIndex).prevOrderIndex = order.prevOrderIndex;
        } else {
            level.tailOrderIndex = order.prevOrderIndex; 
        }

        level.totalVolume -= order.quantity;
        
        // 2. Free Memory (O(1) time)
        orderMap[orderId] = -1;
        pool.freeOrder(orderIndex);

        // (In HFT, we purposely DO NOT update bestBid/bestAsk here to save time. 
        // The matching engine will just skip the empty level later.)
    }
};

// ==========================================
// CUSTOM PERCENTILE STATISTICS
// ==========================================
static double Percentile90(const std::vector<double>& data) {
    if (data.empty()) return 0.0;
    std::vector<double> copy = data;
    std::sort(copy.begin(), copy.end());
    size_t idx = std::ceil(0.90 * copy.size()) - 1;
    return copy[idx];
}

static double Percentile99(const std::vector<double>& data) {
    if (data.empty()) return 0.0;
    std::vector<double> copy = data;
    std::sort(copy.begin(), copy.end());
    size_t idx = std::ceil(0.99 * copy.size()) - 1;
    return copy[idx];
}
// ==========================================
// 3. THE "NO-CHEAT" BENCHMARK SUITE
// ==========================================
#ifndef INGEST_BUILD
// SCENARIO A: Pure CPU Limit (The "Frictionless" Test)
// Measures theoretical maximum speed of your logic inside the L1 Cache.
static void BM_Pure_Crossing(benchmark::State& state) {
    auto tradeLog = std::make_unique<SPSCRingBuffer<100000>>(); 
    auto engine = std::make_unique<LimitOrderBook>(1000000, tradeLog.get()); 
    uint64_t id_counter = 1;

    for (auto _ : state) {
        uint64_t buyId = (id_counter % 500000) + 1;
        uint64_t sellId = buyId + 500000;
        id_counter++;

        engine->addBuyOrder(buyId, 1, 100);
        engine->addSellOrder(sellId, 1, 100);

        benchmark::DoNotOptimize(buyId);
        benchmark::DoNotOptimize(sellId);
    }
}

// SCENARIO B: Real Market (The "Dirty" Test)
// Deep order book, widespread prices, active consumer thread, massive L3 Cache misses.
static void BM_Realistic_Market(benchmark::State& state) {
    auto tradeLog = std::make_unique<SPSCRingBuffer<100000>>(); 
    auto engine = std::make_unique<LimitOrderBook>(1000000, tradeLog.get()); 
    
    // Spin up consumer thread to read trades concurrently
    std::atomic<bool> keepRunning{true};
    std::thread loggerThread([&]() {
        TradeMsg msg;
        while (keepRunning.load(std::memory_order_relaxed)) {
            while (tradeLog->pop(msg)) {} // Drain instantly
        }
    });

    // Warmup: Build a deep, realistic order book
    std::mt19937 rng(42); 
    std::uniform_int_distribution<uint64_t> bidPriceGen(9000, 9900);
    std::uniform_int_distribution<uint64_t> askPriceGen(10100, 11000);
    std::uniform_int_distribution<uint32_t> qtyGen(10, 100);

    uint64_t id_counter = 1;
    for (int i = 0; i < 50000; ++i) {
        engine->addBuyOrder(id_counter++, bidPriceGen(rng), qtyGen(rng));
        engine->addSellOrder(id_counter++, askPriceGen(rng), qtyGen(rng));
    }

    // Benchmark: Spray random market orders testing cache misses and the linear scan
    std::uniform_int_distribution<uint64_t> marketPriceGen(9800, 10200);
    std::uniform_int_distribution<int> sideGen(0, 1);

    for (auto _ : state) {
        uint64_t orderId = (id_counter % 900000) + 1; 
        id_counter++;
        uint64_t price = marketPriceGen(rng);
        
        if (sideGen(rng) == 0) {
            engine->addBuyOrder(orderId, price, 100);
        } else {
            engine->addSellOrder(orderId, price, 100);
        }

        benchmark::DoNotOptimize(orderId);
        benchmark::DoNotOptimize(price);
    }

    // Teardown
    keepRunning.store(false, std::memory_order_release);
    loggerThread.join();
}

// SCENARIO C: 100% Fill Rate under Chaotic Memory (The "Execution Stress" Test)
static void BM_100pct_Crossing(benchmark::State& state) {
    auto tradeLog = std::make_unique<SPSCRingBuffer<100000>>(); 
    auto engine = std::make_unique<LimitOrderBook>(1000000, tradeLog.get()); 
    
    std::atomic<bool> keepRunning{true};
    std::thread loggerThread([&]() {
        TradeMsg msg;
        while (keepRunning.load(std::memory_order_relaxed)) {
            while (tradeLog->pop(msg)) {} 
        }
    });

    
    engine->addSellOrder(999998, 10001, 1); 
    engine->addBuyOrder(999999, 9999, 1);   

    uint64_t base_id = 1;

    for (auto _ : state) {
        
        uint64_t sellId = (base_id % 400000) + 1;
        uint64_t buyId = sellId + 400000;
        base_id++;

        engine->addSellOrder(sellId, 10000, 100);
        engine->addBuyOrder(buyId, 10000, 100);

        benchmark::DoNotOptimize(buyId);
        benchmark::DoNotOptimize(sellId);
    }

    keepRunning.store(false, std::memory_order_release);
    loggerThread.join();
}

// ==========================================
// SCENARIO E: The Pathological Scan (Worst-Case Weakness)
// Forces the engine to scan 19,900 empty memory slots to prove the O(n) design flaw.
// ==========================================
static void BM_Pathological_Scan(benchmark::State& state) {
    auto tradeLog = std::make_unique<SPSCRingBuffer<100000>>(); 
    auto engine = std::make_unique<LimitOrderBook>(1000000, tradeLog.get()); 
    
    // Put a Sentinel Ask incredibly far away
    engine->addSellOrder(999999, 20000, 1); 

    uint64_t base_id = 1;
    for (auto _ : state) {
        uint64_t sellId = (base_id % 400000) + 1;
        uint64_t buyId = sellId + 400000;
        base_id++;

        // 1. Place an Ask at 100. (bestAskPrice drops to 100)
        engine->addSellOrder(sellId, 100, 100);

        // 2. Buy it instantly. The Ask at 100 is now empty.
        // 3. THE TRAP: The engine now scans from 101 all the way to 20,000 looking for the Sentinel!
        engine->addBuyOrder(buyId, 100, 100);

        benchmark::DoNotOptimize(buyId);
        benchmark::DoNotOptimize(sellId);
    }
}

// ==========================================
// SCENARIO F: Order Cancellation (O(1) Speed Test)
// Tests the raw speed of extracting an order from the Doubly-Linked List
// ==========================================
static void BM_Order_Cancellation(benchmark::State& state) {
    auto tradeLog = std::make_unique<SPSCRingBuffer<100000>>(); 
    auto engine = std::make_unique<LimitOrderBook>(1000000, tradeLog.get()); 
    
    uint64_t base_id = 1;
    for (auto _ : state) {
        uint64_t id = (base_id % 900000) + 1;
        base_id++;

        engine->addBuyOrder(id, 10000, 100); // Insert
        engine->cancelOrder(id);             // Instantly Cancel

        benchmark::DoNotOptimize(id);
    }
}

// SCENARIO D: The Baseline Comparison (Standard C++ STL)
// Measures the exact same Realistic Market, but using std::map and std::list.
static void BM_Baseline_STL(benchmark::State& state) {
    auto engine = std::make_unique<LimitOrderBookSTL>(); 

    std::mt19937 rng(42); 
    std::uniform_int_distribution<uint64_t> bidPriceGen(9000, 9900);
    std::uniform_int_distribution<uint64_t> askPriceGen(10100, 11000);
    std::uniform_int_distribution<uint32_t> qtyGen(10, 100);

    uint64_t id_counter = 1;
    for (int i = 0; i < 50000; ++i) {
        engine->addBuyOrder(id_counter++, bidPriceGen(rng), qtyGen(rng));
        engine->addSellOrder(id_counter++, askPriceGen(rng), qtyGen(rng));
    }

    std::uniform_int_distribution<uint64_t> marketPriceGen(9800, 10200);
    std::uniform_int_distribution<int> sideGen(0, 1);

    for (auto _ : state) {
        uint64_t orderId = (id_counter % 900000) + 1; 
        id_counter++;
        uint64_t price = marketPriceGen(rng);
        
        if (sideGen(rng) == 0) {
            engine->addBuyOrder(orderId, price, 100);
        } else {
            engine->addSellOrder(orderId, price, 100);
        }

        benchmark::DoNotOptimize(orderId);
        benchmark::DoNotOptimize(price);
    }
}

// We bumped repetitions to 100 and attached our custom p90/p99 calculators!
BENCHMARK(BM_Pure_Crossing)
    ->Unit(benchmark::kNanosecond)
    ->Repetitions(100)
    ->ComputeStatistics("p90", Percentile90)
    ->ComputeStatistics("p99", Percentile99)
    ->ReportAggregatesOnly(true);

BENCHMARK(BM_Realistic_Market)
    ->Unit(benchmark::kNanosecond)
    ->Repetitions(100)
    ->ComputeStatistics("p90", Percentile90)
    ->ComputeStatistics("p99", Percentile99)
    ->ReportAggregatesOnly(true);

    BENCHMARK(BM_100pct_Crossing)
    ->Unit(benchmark::kNanosecond)
    ->Repetitions(100)
    ->ComputeStatistics("p90", Percentile90)
    ->ComputeStatistics("p99", Percentile99)
    ->ReportAggregatesOnly(true);

    BENCHMARK(BM_Pathological_Scan)
    ->Unit(benchmark::kNanosecond)
    ->Repetitions(100)
    ->ComputeStatistics("p90", Percentile90)
    ->ComputeStatistics("p99", Percentile99)
    ->ReportAggregatesOnly(true);

BENCHMARK(BM_Order_Cancellation)
    ->Unit(benchmark::kNanosecond)
    ->Repetitions(100)
    ->ComputeStatistics("p90", Percentile90)
    ->ComputeStatistics("p99", Percentile99)
    ->ReportAggregatesOnly(true);

    BENCHMARK(BM_Baseline_STL)
    ->Unit(benchmark::kNanosecond)
    ->Repetitions(100)
    ->ComputeStatistics("p90", Percentile90)
    ->ComputeStatistics("p99", Percentile99)
    ->ReportAggregatesOnly(true);


BENCHMARK_MAIN();
#endif