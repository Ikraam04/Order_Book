#pragma once

#include "Order.h"
#include "Trade.h"
#include "OrderPool.h"
#include <map>
#include <vector>

/*
 * OrderBook implementation
 * This class manages the order book, processes incoming orders, matches them, and maintains the state of the book
 * It uses an OrderPool to efficiently manage memory for Order objects
 * The order book maintains bids and asks in sorted maps for efficient matching
 */


// replaces std::deque<Order*> at each price level
// pop_front is just head++ — no memory movement, no block pointer chasing
// all Order* pointers sit in one contiguous vector
struct PriceLevel {
    std::vector<Order*> orders;
    size_t head = 0;

    bool    empty()    const { return head >= orders.size(); }
    Order*  front()    const { return orders[head]; }
    void    pop_front()      { head++; }
    void    push_back(Order* o) { orders.push_back(o); }
    void    clear()          { orders.clear(); head = 0; }

    // iterators that start from the first active slot so range-for and std::find skip consumed entries
    std::vector<Order*>::iterator       begin()       { return orders.begin() + head; }
    std::vector<Order*>::iterator       end()         { return orders.end(); }
    std::vector<Order*>::const_iterator begin() const { return orders.begin() + head; }
    std::vector<Order*>::const_iterator end()   const { return orders.end(); }

    void erase(std::vector<Order*>::iterator it) { orders.erase(it); }
};

// Describes what ultimately happened to the incoming order
enum class OrderStatus : uint8_t {
    Resting,     // unfilled (or partially filled) limit order added to the book
    Filled,      // fully matched immediately (any order type)
    PartialFill, // IoC: partially matched; unfilled remainder was cancelled
    Killed,      // FOK: could not be completely filled; entire order cancelled, no trades executed
};

struct ProcessOrderResult {
    std::vector<Trade> trades;
    uint64_t           new_order_id = 0; // non-zero only when the order is resting in the book
    OrderStatus        status       = OrderStatus::Filled;
};


class OrderBook {
public:
    OrderBook();
    ~OrderBook();

    // main method to process incoming orders
    ProcessOrderResult process_order(Order new_order);

    //other methods
    const std::vector<Trade>& get_trade_history() const;
    bool cancel_order(uint64_t order_id);
    int32_t get_best_bid() const;
    int32_t get_best_ask() const;
    void print_order_book() const;

private:
    // internal state

    std::vector<Trade> executed_trades_;

    // reusable buffer for trades generated per process_order call — avoids a heap alloc every call
    std::vector<Trade> trades_buf_;

    uint64_t next_order_id_ = 1; //incremental order ID generator

    // map to maintain sorted order of price levels
    // Keys are integer ticks (1 tick = $0.01).  int32_t comparison is faster than double.
    std::map<int32_t, PriceLevel, std::greater<int32_t>> bids_;
    std::map<int32_t, PriceLevel, std::less<int32_t>>    asks_;

    // flat array indexed by order_id for O(1) lookup with no hashing or pointer chasing
    // order IDs are sequential integers so this is just a direct index
    std::vector<Order*> order_lookup_;

    // the order pool for memory management
    OrderPool order_pool_;

    friend std::ostream& operator<<(std::ostream& os, const OrderBook& book);

    std::vector<Trade> match_and_fill(Order& new_order);

    // Dry-run for FOK: checks whether the full quantity of `order` can be filled
    // at its limit price without modifying the book.
    bool can_fill_completely(const Order& order) const;
};
