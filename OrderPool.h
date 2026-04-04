#pragma once

// OrderPool.h - memory pool for Order objects
// instead of calling new/delete every time an order comes in (which is slow),
// we pre-allocate a big block of Order objects at startup and hand them out as needed.
// OrderBook grabs one with get_order() and gives it back with return_order() when it's done.
// this keeps allocation cost out of the hot path completely.

#include "Order.h"
#include <vector>
#include <memory>

// pre-allocates a big chunk of Order objects up front so we never have to call
// malloc/free during the actual matching - just grab one from the pool and give it back when done

class OrderPool {
public:
    explicit OrderPool(size_t size);

    // grab an order from the pool - throws if you somehow run out
    Order* get_order();

    // give an order back to the pool so it can be reused
    void return_order(Order* order);

private:
    std::vector<Order> pool_;       // the actual storage - all orders live here
    std::vector<Order*> free_list_; // stack of pointers to the available slots
};
