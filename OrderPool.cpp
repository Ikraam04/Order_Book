
#include "OrderPool.h"
#include <stdexcept>
/*
 * OrderPool implementation
 * This class manages a pool of pre-allocated Order objects to minimize dynamic memory allocations.
 * It provides methods to get an available Order object and to return it back to the pool.
 * When the pool is exhausted, it throws an exception.
 */


OrderPool::OrderPool(size_t size) {
    // pre-allocate the pool of Order objects
    pool_.resize(size);

    // Create the free list, initially containing pointers to every object in the pool
    free_list_.reserve(size);
    for (auto& order : pool_) {
        free_list_.push_back(&order);
    }
}

Order* OrderPool::get_order() {
    if (free_list_.empty()) {
        // when no pool objects are available, throw an exception
        throw std::runtime_error("Order pool exhausted!");
    }

    // give the caller a pointer to an available Order object
    Order* order = free_list_.back();
    free_list_.pop_back();
    return order;
}

void OrderPool::return_order(Order* order) {
    // Return the pointer to the free list to be used again
    free_list_.push_back(order);
}
