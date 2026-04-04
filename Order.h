#pragma once

// Order.h - defines what an order looks like
// an order is the basic unit of everything - it's what gets submitted to the book,
// matched against other orders, and either filled or left resting.
// OrderBook.cpp uses these constantly, OrderPool.h manages the memory for them.

#include <cstdint>

// an order - just a struct, no need for a class here

enum class OrderSide : uint8_t {
    Buy = 0,
    Sell = 1
};

enum class OrderType : uint8_t {
    Limit  = 0, // sits in the book if it doesn't immediately match
    Market = 1, // matches at any price, never sits in the book
    IoC    = 2, // immediate-or-cancel: fill what you can at the limit price, cancel the rest
    FOK    = 3  // fill-or-kill: fill the whole thing right now or cancel it entirely
};

struct Order {
    uint64_t order_id;
    uint64_t client_order_id;
    OrderSide side;
    OrderType type;
    int32_t price;    // price in ticks (1 tick = $0.01, so $100.00 = 10000)
    uint64_t quantity;
    uint64_t seq;     // just a counter that goes up with each order - used for fifo priority
                      // way cheaper than calling chrono::now() on every single order

    Order();
    Order(OrderSide s, OrderType t, int32_t p, uint64_t q);

    bool is_filled() const;
};
