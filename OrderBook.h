#pragma once

#include "Order.h"
#include "Trade.h"
#include "OrderPool.h" // Include the pool
#include <map>         // Using the original std::map
#include <deque>
#include <unordered_map>
#include <vector>

/*
 * OrderBook implementation
 * This class manages the order book, processes incoming orders, matches them, and maintains the state of the book
 * It uses an OrderPool to efficiently manage memory for Order objects
 * The order book maintains bids and asks in sorted maps for efficient matching
 */


struct ProcessOrderResult {
    // ths struct holds the result of processing an order (trades executed and new order id if added to book)
    std::vector<Trade> trades;
    uint64_t new_order_id = 0;
};


class OrderBook {
public:
    OrderBook();
    ~OrderBook();

    // main method to process incoming orders
    ProcessOrderResult process_order(Order new_order);

    //other methods
    const std::vector<Trade>& get_trade_history() const;
    bool cancel_order(uint64_t order_id);
    double get_best_bid() const;
    double get_best_ask() const;
    void print_order_book() const;

private:
    // internal state

    std::vector<Trade> executed_trades_;

    uint64_t next_order_id_ = 1; //incremental order ID generator

    // map to maintain sorted order of price levels
    std::map<double, std::deque<Order*>, std::greater<double>> bids_;
    std::map<double, std::deque<Order*>, std::less<double>> asks_;

    // map to quickly find orders by their ID
    std::unordered_map<uint64_t, Order*> orders_by_id_;

    // the order pool for memory management
    OrderPool order_pool_;

    friend std::ostream& operator<<(std::ostream& os, const OrderBook& book);

    std::vector<Trade> match_and_fill(Order& new_order);
};