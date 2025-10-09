#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include "Order.h"
#include "OrderBook.h"

//for benchmarking performance
void run_performance_benchmark(OrderBook& order_book) {

    // pre-generate a large number of random orders
    const int NUM_ORDERS_TO_PROCESS = 2'500'000;
    std::cout << "Starting benchmark: Pre-generating " << NUM_ORDERS_TO_PROCESS << " orders..." << std::endl;

    // store orders in a vector
    std::vector<Order> orders_to_process;
    orders_to_process.reserve(NUM_ORDERS_TO_PROCESS);

    // rng setup
    std::mt19937 gen(0);
    std::uniform_int_distribution<> side_dist(0, 1);
    std::uniform_int_distribution<> type_dist(0, 1);
    std::uniform_real_distribution<> price_dist(9000.0, 11000.0);
    std::uniform_int_distribution<uint64_t> qty_dist(1, 100);

    // generate orders
    for (int i = 0; i < NUM_ORDERS_TO_PROCESS; ++i) {
        OrderSide side = (side_dist(gen) == 0) ? OrderSide::Buy : OrderSide::Sell;
        OrderType type = (type_dist(gen) == 0) ? OrderType::Limit : OrderType::Market;
        double price = 0.0;
        if (type == OrderType::Limit) {
            price = price_dist(gen) / 100.0; // price between 90.00 and 110.00 to two decimal places
        }
        uint64_t quantity = qty_dist(gen);
        orders_to_process.emplace_back(side, type, price, quantity);
    }

    // benchmark processing
    std::cout << "Order generation complete. Starting processing benchmark..." << std::endl;
    auto start_time = std::chrono::high_resolution_clock::now();
    for (const auto& order : orders_to_process) {
        order_book.process_order(order);
    }
    auto end_time = std::chrono::high_resolution_clock::now();

    std::cout << "Benchmark complete." << std::endl;
    std::chrono::duration<double> elapsed = end_time - start_time;
    std::cout << "Processed " << NUM_ORDERS_TO_PROCESS << " orders in " << elapsed.count() << " seconds." << std::endl;
    std::cout << "Throughput: " << (NUM_ORDERS_TO_PROCESS / elapsed.count()) << " orders/second." << std::endl;
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
    //general_test(order_book);
    run_performance_benchmark(order_book);
    return 0;
}
