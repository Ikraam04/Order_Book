#include "OrderPool.h"
#include <stdexcept>

// OrderPool.cpp - implementation of the memory pool
// see OrderPool.h for the explanation of why this exists

OrderPool::OrderPool(size_t size) {
    // allocate all the order slots up front in one big vector
    // this means they're all contiguous in memory which is good for cache
    pool_.resize(size);

    // fill the free list with pointers to every slot
    // we treat it as a stack - push/pop from the back which is O(1)
    free_list_.reserve(size);
    for (auto& order : pool_) {
        free_list_.push_back(&order);
    }
}

Order* OrderPool::get_order() {
    if (free_list_.empty()) {
        throw std::runtime_error("Order pool exhausted!");
    }

    // pop from the back of the stack - O(1), no searching
    Order* order = free_list_.back();
    free_list_.pop_back();
    return order;
}

void OrderPool::return_order(Order* order) {
    // push back onto the stack so it can be reused next time
    free_list_.push_back(order);
}
