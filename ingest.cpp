#include <iostream>
#include <chrono>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#define INGEST_BUILD
#include "benchmark.cpp" 

using namespace std;

// Ultra-fast custom integer parser to bypass std::stoi
inline uint64_t parse_int(const char*& ptr) {
    uint64_t val = 0;
    while (*ptr >= '0' && *ptr <= '9') {
        val = val * 10 + (*ptr - '0');
        ptr++;
    }
    return val;
}

int main() {
    const char* filepath = "orders.csv";

    // 1. Open the file directly via the Linux Kernel
    int fd = open(filepath, O_RDONLY);
    if (fd == -1) {
        cerr << "Could not open " << filepath << "\n";
        return 1;
    }

    // 2. Get file size
    struct stat sb;
    if (fstat(fd, &sb) == -1) return 1;
    size_t length = sb.st_size;

    // 3. ZERO-COPY MMAP: Map the file directly into RAM
    const char* data = static_cast<const char*>(mmap(nullptr, length, PROT_READ, MAP_PRIVATE, fd, 0));
    if (data == MAP_FAILED) return 1;

    // 4. Boot up the engine using C++ Macros!
#ifdef ENABLE_LOGGING
    cout << "Mode: End-to-End Pipeline (Logging ENABLED)\n";
    auto tradeLog = std::make_unique<SPSCRingBuffer<131072>>(); // Power of 2!
    auto engine = std::make_unique<LimitOrderBook<131072>>(6000000, tradeLog.get()); 

    std::atomic<bool> keepRunning{true};
    std::thread loggerThread([&]() {
        TradeMsg msg;
        uint64_t total_trades = 0;
        while (keepRunning.load(std::memory_order_relaxed)) {
            while (tradeLog->pop(msg)) { total_trades++; }
        }
        while (tradeLog->pop(msg)) { total_trades++; } 
        cout << "Total trades executed & logged: " << total_trades << "\n";
    });
#else
    cout << "Mode: Core Matching Speed (Logging DISABLED - God Mode)\n";
    auto engine = std::make_unique<LimitOrderBook<>>(6000000, nullptr); 
#endif

    cout << "Ingesting 5 Million Orders via mmap...\n";
    
    // Start the High-Resolution Timer!
    auto start_time = chrono::high_resolution_clock::now();

    const char* ptr = data;
    const char* end = data + length;
    uint64_t order_count = 0;

    // 5. The highly-optimized parsing loop
    while (ptr < end) {
        uint64_t orderId = parse_int(ptr);
        if (ptr >= end) break;
        ptr++; // skip comma

        char side = *ptr;
        ptr += 2; // skip side and comma

        // SHORTOUT: If it's a cancel, skip the rest of the math!
        if (side == 'C') {
            engine->cancelOrder(orderId);
            while (ptr < end && *ptr != '\n') ptr++; // Fast-forward to next line
            if (ptr < end) ptr++;
            order_count++;
            continue;
        }

        // If we get here, it's a Buy or Sell. Parse the rest.
        uint64_t price = parse_int(ptr);
        ptr++; // skip comma

        uint32_t qty = parse_int(ptr);
        
        // skip newline (\r\n or \n)
        while (ptr < end && (*ptr == '\r' || *ptr == '\n')) { ptr++; }

        if (side == 'B') {
            engine->addBuyOrder(orderId, price, qty);
        } else if (side == 'S') {
            engine->addSellOrder(orderId, price, qty);
        }
        order_count++;
    }

    auto end_time = chrono::high_resolution_clock::now();
    double elapsed_seconds = chrono::duration<double>(end_time - start_time).count();

    // 6. Calculate Throughput
    cout << "Finished reading and processing " << order_count << " orders.\n";
    cout << "Time taken: " << elapsed_seconds << " seconds.\n";
    cout << "THROUGHPUT: " << (order_count / elapsed_seconds) << " orders per second!\n";

    // Teardown
#ifdef ENABLE_LOGGING
    keepRunning.store(false, std::memory_order_release);
    loggerThread.join();
#endif

    munmap((void*)data, length);
    close(fd);

    return 0;
}