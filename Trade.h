

#pragma once

#include <cstdint>
#include <chrono>
/*
 * Trade implementation
 * This struct represents a trade that has occurred between a buy and sell order
 * It includes the IDs of the orders involved, the price and quantity traded, and a timestamp
 * no need for a cpp file here - just the attributes
 */


//same as order, no need for a class here
struct Trade {
    uint64_t buyer_order_id;
    uint64_t seller_order_id;
    double price;      // price at which trade occurred
    uint64_t quantity; // quantity traded
    std::chrono::high_resolution_clock::time_point timestamp; //when trade occurred
};


