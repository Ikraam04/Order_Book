#pragma once

#include <cstdint>
#include <chrono>

/*
 * Order implementation
 * This class represents an order in the order book with its attributes and basic functionality
 * It includes constructors and a method to check if the order is filled
 */


// enums to represent the side and type of  order.
enum class OrderSide : uint8_t {
    Buy = 0,
    Sell = 1
};

enum class OrderType : uint8_t {
    Limit  = 0, // rests in the book if not immediately matched
    Market = 1, // matches at any price; never rests
    IoC    = 2, // Immediate-or-Cancel: matches at limit price or better, cancels any unfilled remainder
    FOK    = 3  // Fill-or-Kill: must be fully filled immediately at limit price or better, or the whole order is cancelled
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