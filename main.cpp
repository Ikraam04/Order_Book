#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <algorithm>
#include <numeric>
#include "Order.h"
#include "OrderBook.h"

// Pre-generate a pool of random orders used by both benchmarks
static std::vector<Order> generate_orders(int count, std::mt19937& gen) {
    std::uniform_int_distribution<> side_dist(0, 1);
    std::uniform_int_distribution<> type_dist(0, 1);
    std::uniform_real_distribution<> price_dist(9000.0, 11000.0);
    std::uniform_int_distribution<uint64_t> qty_dist(1, 100);

    std::vector<Order> orders;
    orders.reserve(count);
    for (int i = 0; i < count; ++i) {
        OrderSide side = (side_dist(gen) == 0) ? OrderSide::Buy : OrderSide::Sell;
        OrderType type = (type_dist(gen) == 0) ? OrderType::Limit : OrderType::Market;
        double price = (type == OrderType::Limit) ? price_dist(gen) / 100.0 : 0.0;
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
                ++adds;
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(end - start).count();

        std::cout << "\n=== Throughput Benchmark (add + cancel) ===\n";
        std::cout << "  Orders added:           " << adds        << "\n";
        std::cout << "  Cancels (successful):   " << cancels_ok  << "\n";
        std::cout << "  Cancels (already filled): " << cancels_miss << "\n";
        std::cout << "  Total operations:       " << NUM_OPS     << "\n";
        std::cout << "  Time:                   " << elapsed      << " s\n";
        std::cout << "  Throughput:             " << static_cast<long long>(NUM_OPS / elapsed) << " ops/sec\n";
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


void general_test(OrderBook& order_book) {
    // Basic functionality test

    //create some orders
    Order order1(OrderSide::Buy, OrderType::Limit, 100.0, 10);
    Order order2(OrderSide::Sell, OrderType::Limit, 101.0, 5);
    Order order3(OrderSide::Sell, OrderType::Limit, 99.0, 15);
    Order order4(OrderSide::Buy, OrderType::Limit, 105, 100);

    //process orders
    auto result1 = order_book.process_order(order1);
    std::cout << "Processed Order 1: New Order ID = " << result1.new_order_id << std::endl;


    auto result2 = order_book.process_order(order2);
    std::cout << "Processed Order 2: New Order ID = " << result2.new_order_id << std::endl;

    auto result3 = order_book.process_order(order3);
    std::cout << "Processed Order 3: New Order ID = " << result3.new_order_id << std::endl;

    //print trades
    for (const auto &trade: result3.trades) {
        std::cout << "  -> Trade: BuyerID=" << trade.buyer_order_id
                  << ", SellerID=" << trade.seller_order_id
                  << ", Price=" << trade.price
                  << ", Qty=" << trade.quantity << std::endl;
    }


    // process a market order that should match with existing orders
    auto result4 = order_book.process_order(order4);
    std::cout << "Processed Order 4: New Order ID = " << result4.new_order_id << std::endl;
;
    //print trades
    for (const auto &trade: result4.trades) {
        std::cout << "  -> Trade: BuyerID=" << trade.buyer_order_id
                  << ", SellerID=" << trade.seller_order_id
                  << ", Price=" << trade.price
                  << ", Qty=" << trade.quantity << std::endl;
    }


    std::cout << "\nFinal Order Book State:" << std::endl;
    std::cout << order_book << std::endl;
}

int main() {
    OrderBook order_book;
    general_test(order_book);
    std::cout << "Total trades executed: " << order_book.get_trade_history().size() << std::endl;
    run_performance_benchmark();
    return 0;
}