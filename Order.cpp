#include "Order.h"

/*
 * Order implementation
 * This class represents an order in the order book with its attributes and basic functionality
 * It includes constructors and a method to check if the order is filled
 */

// default constructor
Order::Order()
        : order_id(0),
          side(OrderSide::Buy),
          type(OrderType::Limit),
          price(0.0),
          quantity(0),
          timestamp(std::chrono::high_resolution_clock::now())
{}

// parameterized constructor
Order::Order(OrderSide s, OrderType t, double p, uint64_t q)
        : side(s),
          type(t),
          price(p),
          quantity(q),
          timestamp(std::chrono::high_resolution_clock::now()) {
}

// is_filled() member function
bool Order::is_filled() const {
    return quantity == 0;
}