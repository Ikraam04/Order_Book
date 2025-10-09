#pragma once

#include <cstdint>
#include <chrono>

// enums to represent the side and type of  order.
enum class OrderSide : uint8_t {
    Buy = 0,
    Sell = 1
};

enum class OrderType : uint8_t {
    Limit = 0,
    Market = 1
};
//struct because why not? no need for a class here
struct Order {
    uint64_t order_id;
    uint64_t client_order_id;
    OrderSide side;
    OrderType type;
    double price;
    uint64_t quantity;
    std::chrono::high_resolution_clock::time_point timestamp;


    Order();


    Order(OrderSide s, OrderType t, double p, uint64_t q);

    bool is_filled() const;
};