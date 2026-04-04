#include "OrderBook.h"
#include <algorithm>
#include <iostream>
#include <iomanip>

// OrderBook.cpp - implementation of the order book
// the two main functions are process_order() which handles everything coming in,
// and match_and_fill() which does the actual price-time priority matching.
// most of the optimisation work lives in here - memory pool usage, reusable trade buffer,
// flat array lookups etc. check OPTIMISATIONS.md if you want to know why things are the way they are.

OrderBook::OrderBook() : order_pool_(2'500'000) {
    // reserve everything upfront so we never reallocate mid-benchmark
    executed_trades_.reserve(2'500'000);
    trades_buf_.reserve(64); // most orders don't generate more than a handful of trades

    // +1 because order IDs start at 1, index 0 is just unused
    order_lookup_.assign(2'500'001, nullptr);
}

OrderBook::~OrderBook() {
}

ProcessOrderResult OrderBook::process_order(Order new_order_data) {
    ProcessOrderResult result;

    // grab a slot from the pool instead of calling new - no heap allocation
    Order* incoming_order = order_pool_.get_order();
    incoming_order->order_id = next_order_id_++;
    incoming_order->seq      = incoming_order->order_id; // same value, just used for fifo ordering
    incoming_order->side     = new_order_data.side;
    incoming_order->type     = new_order_data.type;
    incoming_order->price    = new_order_data.price;
    incoming_order->quantity = new_order_data.quantity;

    // FOK needs a dry run first - if we can't fill the whole thing, kill it without touching the book
    if (incoming_order->type == OrderType::FOK && !can_fill_completely(*incoming_order)) {
        order_pool_.return_order(incoming_order);
        result.status = OrderStatus::Killed;
        return result;
    }

    // do the matching - trades go into trades_buf_
    match_and_fill(*incoming_order);

    // result.trades is just a span pointing at trades_buf_ - no copy happens here
    result.trades = trades_buf_;

    if (!incoming_order->is_filled() && incoming_order->type == OrderType::Limit) {
        // unfilled limit order - add it to the book
        if (incoming_order->side == OrderSide::Buy) {
            bids_[incoming_order->price].push_back(incoming_order);
        } else {
            asks_[incoming_order->price].push_back(incoming_order);
        }
        // store in the lookup array so cancel_order can find it in O(1)
        order_lookup_[incoming_order->order_id] = incoming_order;
        result.new_order_id = incoming_order->order_id;
        result.status = OrderStatus::Resting;
    } else {
        // market / IoC / FOK that filled - doesn't rest in the book, give memory back to pool
        if (!incoming_order->is_filled() && incoming_order->type == OrderType::IoC) {
            result.status = OrderStatus::PartialFill;
        } else {
            result.status = OrderStatus::Filled;
        }
        order_pool_.return_order(incoming_order);
    }

    return result;
}

void OrderBook::match_and_fill(Order& incoming_order) {
    // clear the buffer from the last call - doesn't free memory, just resets the size counter
    std::vector<Trade>& trades = trades_buf_;
    trades.clear();

    if (incoming_order.side == OrderSide::Buy) {
        // buy order - walk asks from cheapest upward
        // stop when: no more asks, order is full, or price is too high (not applicable for market orders)
        auto it = asks_.begin();
        while (it != asks_.end() && incoming_order.quantity > 0 &&
               (incoming_order.type == OrderType::Market || incoming_order.price >= it->first)) {

            auto& orders_at_price = it->second;

            // match against every order at this price level, fifo
            while (!orders_at_price.empty() && incoming_order.quantity > 0) {
                Order* existing_sell_order = orders_at_price.front();
                uint64_t trade_quantity = std::min(incoming_order.quantity, existing_sell_order->quantity);

                Trade new_trade = {
                    incoming_order.order_id,
                    existing_sell_order->order_id,
                    it->first,
                    trade_quantity
                };
                trades.push_back(new_trade);
                executed_trades_.push_back(new_trade);

                incoming_order.quantity -= trade_quantity;
                existing_sell_order->quantity -= trade_quantity;

                if (existing_sell_order->is_filled()) {
                    orders_at_price.pop_front(); // just moves head++ in PriceLevel, very cheap
                    order_lookup_[existing_sell_order->order_id] = nullptr;
                    order_pool_.return_order(existing_sell_order);
                }
            }

            // if price level is empty now, remove it from the map
            if (orders_at_price.empty()) {
                it = asks_.erase(it);
            } else {
                ++it;
            }
        }

    } else {
        // sell order - walk bids from highest downward
        auto it = bids_.begin();
        while (it != bids_.end() && incoming_order.quantity > 0 &&
               (incoming_order.type == OrderType::Market || incoming_order.price <= it->first)) {

            auto& orders_at_price = it->second;

            while (!orders_at_price.empty() && incoming_order.quantity > 0) {
                Order* existing_buy_order = orders_at_price.front();
                uint64_t trade_quantity = std::min(incoming_order.quantity, existing_buy_order->quantity);

                Trade new_trade = {existing_buy_order->order_id, incoming_order.order_id, it->first, trade_quantity};
                trades.push_back(new_trade);
                executed_trades_.push_back(new_trade);

                incoming_order.quantity -= trade_quantity;
                existing_buy_order->quantity -= trade_quantity;

                if (existing_buy_order->is_filled()) {
                    orders_at_price.pop_front();
                    order_lookup_[existing_buy_order->order_id] = nullptr;
                    order_pool_.return_order(existing_buy_order);
                }
            }

            if (orders_at_price.empty()) {
                it = bids_.erase(it);
            } else {
                ++it;
            }
        }
    }
}

bool OrderBook::can_fill_completely(const Order& order) const {
    // FOK dry run - walk the opposite side of the book and add up available quantity
    // we stop early as soon as we know there's enough, so it's fast in the common case
    // doesn't modify the book at all
    uint64_t available = 0;

    if (order.side == OrderSide::Buy) {
        for (const auto& [price, orders_at_price] : asks_) {
            if (price > order.price) break; // asks are sorted low-to-high, so we can stop here
            for (const Order* o : orders_at_price) {
                available += o->quantity;
                if (available >= order.quantity) return true;
            }
        }
    } else {
        for (const auto& [price, orders_at_price] : bids_) {
            if (price < order.price) break; // bids are sorted high-to-low, same idea
            for (const Order* o : orders_at_price) {
                available += o->quantity;
                if (available >= order.quantity) return true;
            }
        }
    }
    return false;
}

bool OrderBook::cancel_order(uint64_t order_id) {
    // direct array lookup by order ID - O(1), no hashing needed
    // order IDs are sequential so we just use them as indices
    if (order_id >= order_lookup_.size() || order_lookup_[order_id] == nullptr) {
        return false; // order doesn't exist or was already filled
    }

    Order* order_to_cancel = order_lookup_[order_id];
    order_lookup_[order_id] = nullptr;

    // find the order in its price level and remove it
    // this is O(n) on the level size but cancels are rare so it's fine
    if (order_to_cancel->side == OrderSide::Buy) {
        auto& orders_at_price = bids_.at(order_to_cancel->price);
        auto order_it = std::find(orders_at_price.begin(), orders_at_price.end(), order_to_cancel);
        if (order_it != orders_at_price.end()) {
            orders_at_price.erase(order_it);
            if (orders_at_price.empty()) {
                bids_.erase(order_to_cancel->price);
            }
        }
    } else {
        auto& orders_at_price = asks_.at(order_to_cancel->price);
        auto order_it = std::find(orders_at_price.begin(), orders_at_price.end(), order_to_cancel);
        if (order_it != orders_at_price.end()) {
            orders_at_price.erase(order_it);
            if (orders_at_price.empty()) {
                asks_.erase(order_to_cancel->price);
            }
        }
    }

    order_pool_.return_order(order_to_cancel);
    return true;
}

int32_t OrderBook::get_best_bid() const {
    if (bids_.empty()) return 0;
    return bids_.begin()->first;
}

int32_t OrderBook::get_best_ask() const {
    if (asks_.empty()) return 0;
    return asks_.begin()->first;
}

const std::vector<Trade>& OrderBook::get_trade_history() const {
    return executed_trades_;
}

// formats a tick price as "$DDD.CC (TTTT ticks)"
static std::string fmt_tick(int32_t ticks) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "$%d.%02d (%d ticks)", ticks / 100, ticks % 100, ticks);
    return buf;
}

std::ostream& operator<<(std::ostream& os, const OrderBook& book) {
    os << "Order Book State (1 tick = $0.01):\n";

    os << "  Asks (best first):\n";
    // asks are sorted low-to-high so we print in reverse so the best ask is closest to the spread
    for (auto it = book.asks_.rbegin(); it != book.asks_.rend(); ++it) {
        os << "    " << fmt_tick(it->first) << " : ";
        for (const Order* o : it->second)
            os << "[id=" << o->order_id << " qty=" << o->quantity << "] ";
        os << "\n";
    }

    os << "  --- spread ---\n";

    os << "  Bids (best first):\n";
    for (const auto& [price, orders] : book.bids_) {
        os << "    " << fmt_tick(price) << " : ";
        for (const Order* o : orders)
            os << "[id=" << o->order_id << " qty=" << o->quantity << "] ";
        os << "\n";
    }

    return os;
}
