#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <algorithm>
#include <numeric>
#include <cmath>
#include "Order.h"
#include "OrderBook.h"

// Displays a tick price as a dollar amount alongside the raw tick value
static std::string fmt_price(int32_t ticks) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "$%d.%02d (%d)", ticks / 100, ticks % 100, ticks);
    return buf;
}

// Generates a mid-price path using a symmetric random walk (±1 tick per step).
// Clamped to [9000, 11000] so the mid never drifts more than $10 from the $100 start.
// This ensures the benchmark exercises the matching engine continuously as the mid drifts
// into resting orders, rather than just benchmarking book insertion.
static std::vector<int32_t> generate_mid_path(int count, std::mt19937& gen) {
    std::vector<int32_t> path;
    path.reserve(count);
    int32_t mid = 10000; // start at $100.00
    std::uniform_int_distribution<int32_t> step_dist(-1, 1); // -1, 0, or +1 tick each step
    for (int i = 0; i < count; ++i) {
        path.push_back(mid);
        mid = std::clamp(mid + step_dist(gen), int32_t{9000}, int32_t{11000});
    }
    return path;
}

// Pre-generates orders whose prices are drawn from N(mid, σ=5 ticks).
// σ=5 means ~68% of orders land within 5 ticks of mid — orders cross the spread often,
// so the benchmark exercises both the matching and the insertion paths.
// Mix: 40% Limit, 20% Market, 20% IoC, 20% FOK
static std::vector<Order> generate_orders(const std::vector<int32_t>& mid_path, std::mt19937& gen) {
    const int count = static_cast<int>(mid_path.size());
    std::uniform_int_distribution<>         side_dist(0, 1);
    std::uniform_int_distribution<>         type_dist(0, 4);
    std::normal_distribution<double>        offset_dist(0.0, 5.0); // σ = 5 ticks
    std::uniform_int_distribution<uint64_t> qty_dist(1, 100);

    std::vector<Order> orders;
    orders.reserve(count);
    for (int i = 0; i < count; ++i) {
        OrderSide side = (side_dist(gen) == 0) ? OrderSide::Buy : OrderSide::Sell;
        int t = type_dist(gen);
        OrderType type;
        if      (t <= 1) type = OrderType::Limit;  // 40%
        else if (t == 2) type = OrderType::Market; // 20%
        else if (t == 3) type = OrderType::IoC;    // 20%
        else             type = OrderType::FOK;    // 20%

        // Market orders have no price limit; all others are centred on the current mid
        int32_t price = 0;
        if (type != OrderType::Market) {
            price = static_cast<int32_t>(std::round(mid_path[i] + offset_dist(gen)));
            price = std::max(price, int32_t{1}); // guard against negative prices
        }
        orders.emplace_back(side, type, price, qty_dist(gen));
    }
    return orders;
}

void run_performance_benchmark() {
    const int NUM_OPS        = 2'500'000;
    const double CANCEL_RATIO = 0.20; // ~20% of operations are cancellations

    std::mt19937 gen(42);
    std::cout << "Pre-generating " << NUM_OPS << " orders (random-walk mid, N(mid,σ=5) prices)...\n";
    auto mid_path = generate_mid_path(NUM_OPS, gen);
    auto orders   = generate_orders(mid_path, gen);
    std::cout << "  Mid range: " << fmt_price(*std::min_element(mid_path.begin(), mid_path.end()))
              << " – " << fmt_price(*std::max_element(mid_path.begin(), mid_path.end())) << "\n";

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
                  << " price=" << fmt_price(t.price)
                  << " qty=" << t.quantity << "\n";
    }
}

void general_test(OrderBook& order_book) {
    // All prices are in ticks (1 tick = $0.01).
    // 10000 = $100.00,  10100 = $101.00,  9900 = $99.00,  10500 = $105.00
    std::cout << "=== Basic Limit/Market tests ===\n";

    auto r1 = order_book.process_order({OrderSide::Buy,  OrderType::Limit, 10000, 10});
    auto r2 = order_book.process_order({OrderSide::Sell, OrderType::Limit, 10100,  5});
    print_result("Limit Buy  @$100.00 qty10 (expect Resting)", r1);
    print_result("Limit Sell @$101.00 qty5  (expect Resting)", r2);

    // Sell@$99 crosses the bid@$100 — should match
    auto r3 = order_book.process_order({OrderSide::Sell, OrderType::Limit, 9900, 15});
    print_result("Limit Sell @$99.00 qty15  (expect partial fill + Resting)", r3);

    // Aggressive buy sweeps remaining book
    auto r4 = order_book.process_order({OrderSide::Buy, OrderType::Limit, 10500, 100});
    print_result("Limit Buy  @$105.00 qty100 (expect trades + Resting remainder)", r4);

    std::cout << "\n=== IoC tests ===\n";

    order_book.process_order({OrderSide::Sell, OrderType::Limit, 10000, 20}); // seed: sell@$100 qty20

    auto ioc1 = order_book.process_order({OrderSide::Buy, OrderType::IoC, 9900, 10});
    print_result("IoC Buy @$99.00 qty10 vs ask@$100 (expect Filled=0, no resting)", ioc1);

    auto ioc2 = order_book.process_order({OrderSide::Buy, OrderType::IoC, 10000, 30});
    print_result("IoC Buy @$100.00 qty30 vs ask@$100 qty20 (expect PartialFill)", ioc2);

    std::cout << "\n=== FOK tests ===\n";

    order_book.process_order({OrderSide::Sell, OrderType::Limit, 10000, 5}); // seed: sell@$100 qty5

    auto fok1 = order_book.process_order({OrderSide::Buy, OrderType::FOK, 10000, 20});
    print_result("FOK Buy @$100.00 qty20 vs ask@$100 qty5  (expect Killed)", fok1);

    auto fok2 = order_book.process_order({OrderSide::Buy, OrderType::FOK, 10000, 5});
    print_result("FOK Buy @$100.00 qty5  vs ask@$100 qty5  (expect Filled)", fok2);

    std::cout << "\nFinal Order Book State:\n" << order_book << "\n";
}

int main() {
    OrderBook order_book;
    general_test(order_book);
    std::cout << "Total trades recorded in history: " << order_book.get_trade_history().size() << "\n\n";
    run_performance_benchmark();
    return 0;
}