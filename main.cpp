#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <algorithm>
#include <numeric>
#include "Order.h"
#include "OrderBook.h"

// Pre-generate a pool of random orders used by both benchmarks.
// Mix: 40% Limit, 20% Market, 20% IoC, 20% FOK
// Prices use a tighter spread (mid=100, ±5) so orders cross more often — a more realistic workload.
static std::vector<Order> generate_orders(int count, std::mt19937& gen) {
    std::uniform_int_distribution<>    side_dist(0, 1);
    std::uniform_int_distribution<>    type_dist(0, 4);   // 0=Limit,1=Market,2=IoC,3-4=FOK (weighted)
    std::uniform_real_distribution<>   price_dist(9500.0, 10500.0); // ±5% of mid=100
    std::uniform_int_distribution<uint64_t> qty_dist(1, 100);

    std::vector<Order> orders;
    orders.reserve(count);
    for (int i = 0; i < count; ++i) {
        OrderSide side = (side_dist(gen) == 0) ? OrderSide::Buy : OrderSide::Sell;
        int t = type_dist(gen);
        OrderType type;
        if      (t <= 1) type = OrderType::Limit;   // 0,1 → 40%
        else if (t == 2) type = OrderType::Market;  // 2   → 20%
        else if (t == 3) type = OrderType::IoC;     // 3   → 20%
        else             type = OrderType::FOK;     // 4   → 20%

        // Market orders have no price; all others use a limit price
        double price = (type == OrderType::Market) ? 0.0 : price_dist(gen) / 100.0;
        orders.emplace_back(side, type, price, qty_dist(gen));
    }
    return orders;
}

void run_performance_benchmark() {
    const int NUM_OPS        = 2'500'000;
    const double CANCEL_RATIO = 0.20; // ~20% of operations are cancellations

    std::mt19937 gen(42);
    std::cout << "Pre-generating " << NUM_OPS << " orders..." << std::endl;
    auto orders = generate_orders(NUM_OPS, gen);

    // ---------------------------------------------------------------
    // 1. Throughput benchmark: mixed add + cancel workload
    // ---------------------------------------------------------------
    {
        OrderBook book;
        std::vector<uint64_t> active_ids;
        active_ids.reserve(100'000);

        std::uniform_real_distribution<double> uniform01(0.0, 1.0);
        uint64_t adds = 0, cancels_ok = 0, cancels_miss = 0;
        int order_idx = 0;

        auto start = std::chrono::high_resolution_clock::now();

        uint64_t fok_killed = 0, ioc_partial = 0;

        for (int i = 0; i < NUM_OPS; ++i) {
            if (!active_ids.empty() && uniform01(gen) < CANCEL_RATIO) {
                // pick a random active order — swap-remove keeps this O(1)
                auto idx = static_cast<size_t>(uniform01(gen) * active_ids.size());
                uint64_t id = active_ids[idx];
                active_ids[idx] = active_ids.back();
                active_ids.pop_back();

                if (book.cancel_order(id)) ++cancels_ok;
                else                       ++cancels_miss; // already matched/filled
            } else {
                auto result = book.process_order(orders[order_idx++ % NUM_OPS]);
                if (result.new_order_id != 0)
                    active_ids.push_back(result.new_order_id);
                if (result.status == OrderStatus::Killed)      ++fok_killed;
                if (result.status == OrderStatus::PartialFill) ++ioc_partial;
                ++adds;
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(end - start).count();

        std::cout << "\n=== Throughput Benchmark (add + cancel) ===\n";
        std::cout << "  Orders submitted:         " << adds          << "\n";
        std::cout << "  FOK killed:               " << fok_killed    << "\n";
        std::cout << "  IoC partial fills:        " << ioc_partial   << "\n";
        std::cout << "  Cancels (successful):     " << cancels_ok    << "\n";
        std::cout << "  Cancels (already filled): " << cancels_miss  << "\n";
        std::cout << "  Total operations:         " << NUM_OPS       << "\n";
        std::cout << "  Time:                     " << elapsed        << " s\n";
        std::cout << "  Throughput:               " << static_cast<long long>(NUM_OPS / elapsed) << " ops/sec\n";
    }

    // ---------------------------------------------------------------
    // 2. Latency benchmark: per-operation timing, add-only
    //    Uses a smaller sample so the timing overhead stays negligible.
    // ---------------------------------------------------------------
    {
        const int LATENCY_OPS = 200'000;
        OrderBook book;
        std::vector<double> latencies_ns;
        latencies_ns.reserve(LATENCY_OPS);

        for (int i = 0; i < LATENCY_OPS; ++i) {
            auto t0 = std::chrono::high_resolution_clock::now();
            book.process_order(orders[i % NUM_OPS]);
            auto t1 = std::chrono::high_resolution_clock::now();
            latencies_ns.push_back(
                std::chrono::duration<double, std::nano>(t1 - t0).count());
        }

        std::sort(latencies_ns.begin(), latencies_ns.end());
        double mean = std::accumulate(latencies_ns.begin(), latencies_ns.end(), 0.0) / LATENCY_OPS;

        auto pct = [&](double p) {
            return latencies_ns[static_cast<size_t>(p / 100.0 * LATENCY_OPS)];
        };

        std::cout << "\n=== Latency Benchmark (add-only, " << LATENCY_OPS << " ops) ===\n";
        std::cout << "  mean: " << mean          << " ns\n";
        std::cout << "  p50:  " << pct(50)       << " ns\n";
        std::cout << "  p99:  " << pct(99)       << " ns\n";
        std::cout << "  p99.9:" << pct(99.9)     << " ns\n";
        std::cout << "  max:  " << latencies_ns.back() << " ns\n";
    }
}


static const char* status_str(OrderStatus s) {
    switch (s) {
        case OrderStatus::Resting:     return "Resting";
        case OrderStatus::Filled:      return "Filled";
        case OrderStatus::PartialFill: return "PartialFill (IoC)";
        case OrderStatus::Killed:      return "Killed (FOK)";
    }
    return "Unknown";
}

static void print_result(const char* label, const ProcessOrderResult& r) {
    std::cout << label << ": status=" << status_str(r.status)
              << ", resting_id=" << r.new_order_id
              << ", trades=" << r.trades.size() << "\n";
    for (const auto& t : r.trades) {
        std::cout << "    Trade: buyer=" << t.buyer_order_id
                  << " seller=" << t.seller_order_id
                  << " price=" << t.price
                  << " qty=" << t.quantity << "\n";
    }
}

void general_test(OrderBook& order_book) {
    std::cout << "=== Basic Limit/Market tests ===\n";

    // Seed book: buy@100 qty10, sell@101 qty5
    auto r1 = order_book.process_order({OrderSide::Buy,  OrderType::Limit, 100.0, 10});
    auto r2 = order_book.process_order({OrderSide::Sell, OrderType::Limit, 101.0,  5});
    print_result("Limit Buy  @100 qty10 (expect Resting)", r1);
    print_result("Limit Sell @101 qty5  (expect Resting)", r2);

    // Sell@99 crosses the bid@100 — should match
    auto r3 = order_book.process_order({OrderSide::Sell, OrderType::Limit, 99.0, 15});
    print_result("Limit Sell @99  qty15 (expect partial fill + Resting)", r3);

    // Aggressive buy sweeps remaining book
    auto r4 = order_book.process_order({OrderSide::Buy, OrderType::Limit, 105.0, 100});
    print_result("Limit Buy  @105 qty100 (expect trades + Resting remainder)", r4);

    std::cout << "\n=== IoC tests ===\n";

    // Setup: sell@100 qty20
    order_book.process_order({OrderSide::Sell, OrderType::Limit, 100.0, 20});

    // IoC buy below best ask — no match, entire order cancelled (not resting)
    auto ioc1 = order_book.process_order({OrderSide::Buy, OrderType::IoC, 99.0, 10});
    print_result("IoC Buy @99 qty10 against ask@100  (expect Killed/no trades)", ioc1);

    // IoC buy at ask — partial fill possible (only 20 available, want 30), remainder cancelled
    auto ioc2 = order_book.process_order({OrderSide::Buy, OrderType::IoC, 100.0, 30});
    print_result("IoC Buy @100 qty30 vs ask@100 qty20 (expect PartialFill, 1 trade)", ioc2);

    std::cout << "\n=== FOK tests ===\n";

    // Setup: sell@100 qty5
    order_book.process_order({OrderSide::Sell, OrderType::Limit, 100.0, 5});

    // FOK buy qty20 — only 5 available, cannot fill entirely → killed
    auto fok1 = order_book.process_order({OrderSide::Buy, OrderType::FOK, 100.0, 20});
    print_result("FOK Buy @100 qty20 vs ask@100 qty5  (expect Killed, 0 trades)", fok1);

    // FOK buy qty5 — exactly 5 available → fully filled
    auto fok2 = order_book.process_order({OrderSide::Buy, OrderType::FOK, 100.0, 5});
    print_result("FOK Buy @100 qty5  vs ask@100 qty5  (expect Filled, 1 trade)", fok2);

    std::cout << "\nFinal Order Book State:\n" << order_book << "\n";
}

int main() {
    OrderBook order_book;
    general_test(order_book);
    std::cout << "Total trades recorded in history: " << order_book.get_trade_history().size() << "\n\n";
    run_performance_benchmark();
    return 0;
}