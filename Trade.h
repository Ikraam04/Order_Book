

#ifndef ORDER_BOOK_TRADE_H
#define ORDER_BOOK_TRADE_H
#pragma once

#include <cstdint>
#include <chrono>

//same as order, no need for a class here
struct Trade {
    uint64_t buyer_order_id;
    uint64_t seller_order_id;
    double price;      // price at which trade occurred
    uint64_t quantity; // quantity traded
    std::chrono::high_resolution_clock::time_point timestamp; //when trade occurred
};

#endif
