//
// Created by taco on 06/09/2025.
//

#ifndef ORDER_BOOK_ORDERPOOL_H
#define ORDER_BOOK_ORDERPOOL_H

#pragma once

#include "Order.h"
#include <vector>
#include <memory>

class OrderPool {
public:
    // Constructor: Pre-allocates a fixed number of Order objects.
    explicit OrderPool(size_t size);

    // Gets an available Order object from the pool.
    // Throws std::runtime_error if the pool is exhausted.
    Order* get_order();

    // Returns an Order object to the pool so it can be reused.
    void return_order(Order* order);

private:
    // This vector owns all the Order objects for their entire lifetime.
    std::vector<Order> pool_;

    // This stores pointers to the currently available objects in the pool.
    std::vector<Order*> free_list_;
};


#endif //ORDER_BOOK_ORDERPOOL_H
