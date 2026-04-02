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
          price(0),
          quantity(0),
          seq(0)
{}

// parameterized constructor
Order::Order(OrderSide s, OrderType t, int32_t p, uint64_t q)
        : order_id(0),
          client_order_id(0),
          side(s),
          type(t),
          price(p),
          quantity(q),
          seq(0) {
}

// is_filled() member function
bool Order::is_filled() const {
    return quantity == 0;
}