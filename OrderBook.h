#pragma once

// OrderBook.h - the main class that runs everything
// this is where the actual order book lives - it holds the bid/ask price levels,
// matches incoming orders against resting ones, handles cancels, and tracks trade history.
// the core flow is: process_order() -> match_and_fill() -> results come back via ProcessOrderResult.
// a lot of the data structures here have been optimised pretty heavily - see OPTIMISATIONS.md for the full story.

#include "Order.h"
#include "Trade.h"
#include "OrderPool.h"
#include <map>
#include <span>
#include <vector>

// PriceLevel replaces std::deque<Order*> at each price level in the book
// the key thing here is pop_front() - with a deque that involves block pointer chasing
// with this it's literally just head++ which is basically free
// all the order pointers sit in one contiguous vector so the cpu prefetcher is happy
struct PriceLevel {
    std::vector<Order*> orders;
    size_t head = 0; // index of the first live order - everything before this has been matched

    bool   empty()    const { return head >= orders.size(); }
    Order* front()    const { return orders[head]; }
    void   pop_front()      { head++; } // just move the head forward, no memory freed
    void   push_back(Order* o) { orders.push_back(o); }
    void   clear()          { orders.clear(); head = 0; }

    // iterators start from head so range-for and std::find only see live orders
    std::vector<Order*>::iterator       begin()       { return orders.begin() + head; }
    std::vector<Order*>::iterator       end()         { return orders.end(); }
    std::vector<Order*>::const_iterator begin() const { return orders.begin() + head; }
    std::vector<Order*>::const_iterator end()   const { return orders.end(); }

    void erase(std::vector<Order*>::iterator it) { orders.erase(it); }
};

// what happened to an order after process_order runs
enum class OrderStatus : uint8_t {
    Resting,     // limit order sitting in the book (maybe partially filled)
    Filled,      // completely matched
    PartialFill, // IoC: matched what it could, rest was cancelled
    Killed,      // FOK: couldn't fill the whole thing so the whole thing was cancelled
};

struct ProcessOrderResult {
    // span instead of vector - this is just a pointer+size pointing at the orderbook's internal
    // trades buffer. no copy, no allocation. just don't use it after the next process_order call
    std::span<const Trade> trades;
    uint64_t new_order_id = 0; // only set if the order is now resting in the book
    OrderStatus status = OrderStatus::Filled;
};


class OrderBook {
public:
    OrderBook();
    ~OrderBook();

    ProcessOrderResult process_order(Order new_order);

    const std::vector<Trade>& get_trade_history() const;
    bool cancel_order(uint64_t order_id);
    int32_t get_best_bid() const;
    int32_t get_best_ask() const;
    void print_order_book() const;

private:
    std::vector<Trade> executed_trades_; // full history of every trade - pre-reserved so it never reallocates

    // reusable buffer - match_and_fill writes trades here instead of allocating a new vector every call
    // the result span points into this, so the caller sees the trades without any copying
    std::vector<Trade> trades_buf_;

    uint64_t next_order_id_ = 1;

    // bids sorted high-to-low (best bid first), asks sorted low-to-high (best ask first)
    // using int32_t ticks as the key - way faster than comparing doubles
    std::map<int32_t, PriceLevel, std::greater<int32_t>> bids_;
    std::map<int32_t, PriceLevel, std::less<int32_t>>    asks_;

    // flat array indexed by order_id for O(1) cancel lookup
    // order IDs are just sequential ints starting at 1 so we can use them directly as indices
    // way faster than unordered_map which has to hash + chase pointers through heap nodes
    std::vector<Order*> order_lookup_;

    OrderPool order_pool_;

    friend std::ostream& operator<<(std::ostream& os, const OrderBook& book);

    void match_and_fill(Order& new_order); // writes into trades_buf_, no return value

    // used for FOK only - dry run to check if we can fill the whole order before touching the book
    bool can_fill_completely(const Order& order) const;
};
