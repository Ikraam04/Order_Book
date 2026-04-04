#include "Order.h"

// Order.cpp - just the constructors and is_filled() for the Order struct
// nothing fancy here, Order.h has all the actual field definitions

// default constructor - everything zeroed out
Order::Order()
        : order_id(0),
          side(OrderSide::Buy),
          type(OrderType::Limit),
          price(0),
          quantity(0),
          seq(0)
{}

// the one you actually use when submitting an order
Order::Order(OrderSide s, OrderType t, int32_t p, uint64_t q)
        : order_id(0),
          client_order_id(0),
          side(s),
          type(t),
          price(p),
          quantity(q),
          seq(0) {
}

// order is done when quantity hits 0
bool Order::is_filled() const {
    return quantity == 0;
}
