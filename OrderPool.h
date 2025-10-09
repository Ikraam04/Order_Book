//
// Created by taco on 06/09/2025.
//


#pragma once

#include "Order.h"
#include <vector>
#include <memory>

/*
 * OrderPool implementation
 * This class manages a pool of pre-allocated Order objects to minimize dynamic memory allocations.
 * It provides methods to get an available Order object and to return it back to the pool.
 * When the pool is exhausted, it throws an exception.
 */

class OrderPool {
public:
    // constructor pre-allocates a fixed number of Order objects
    explicit OrderPool(size_t size);

    // returns a pointer to an available Order object from the pool
    // throws std::runtime_error if the pool is exhausted
    Order* get_order();

    // returns an Order object back to the pool for reuse
    void return_order(Order* order);

private:
    // this is the actual storage for the pool of Order objects
    std::vector<Order> pool_;

    // this is a stack of pointers to available Order objects in the pool
    std::vector<Order*> free_list_;
};


