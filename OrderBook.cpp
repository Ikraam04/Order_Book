#include "OrderBook.h"
#include <algorithm>
#include <iostream>
#include <iomanip>
/*
 * OrderBook implementation
 * This class manages the order book, processes incoming orders, matches them, and maintains the state of the book
 * It uses an OrderPool to efficiently manage memory for Order objects
 * The order book maintains bids and asks in sorted maps for efficient matching
 */


//constructor
OrderBook::OrderBook() : order_pool_(2'500'000) {
    executed_trades_.reserve(2'500'000);
    trades_buf_.reserve(64);
    // +1 because order IDs start at 1, so index 0 is unused
    order_lookup_.assign(2'500'001, nullptr);
}

// destructor
OrderBook::~OrderBook() {
}

ProcessOrderResult OrderBook::process_order(Order new_order_data) {
    ProcessOrderResult result;

    Order* incoming_order = order_pool_.get_order();
    incoming_order->order_id = next_order_id_++;
    incoming_order->seq      = incoming_order->order_id; // seq == order_id: monotonically increasing, free
    incoming_order->side     = new_order_data.side;
    incoming_order->type     = new_order_data.type;
    incoming_order->price    = new_order_data.price;
    incoming_order->quantity = new_order_data.quantity;

    // FOK: dry-run check — if we cannot fill the entire quantity right now, kill the order
    if (incoming_order->type == OrderType::FOK && !can_fill_completely(*incoming_order)) {
        order_pool_.return_order(incoming_order);
        result.status = OrderStatus::Killed;
        return result;
    }

    match_and_fill(*incoming_order);
    result.trades = trades_buf_; // span — zero-copy view into the buffer

    if (!incoming_order->is_filled() && incoming_order->type == OrderType::Limit) {
        // Unfilled limit order — add it to the book
        if (incoming_order->side == OrderSide::Buy) {
            bids_[incoming_order->price].push_back(incoming_order);
        } else {
            asks_[incoming_order->price].push_back(incoming_order);
        }
        order_lookup_[incoming_order->order_id] = incoming_order;
        result.new_order_id = incoming_order->order_id;
        result.status = result.trades.empty() ? OrderStatus::Resting : OrderStatus::Resting;
    } else {
        // Market / IoC / FOK (filled) — never rest; return memory to pool
        if (!incoming_order->is_filled() && incoming_order->type == OrderType::IoC) {
            result.status = OrderStatus::PartialFill; // IoC: some filled, rest cancelled
        } else {
            result.status = OrderStatus::Filled;
        }
        order_pool_.return_order(incoming_order);
    }

    return result;
}

void OrderBook::match_and_fill(Order& incoming_order) {
    std::vector<Trade>& trades = trades_buf_;
    trades.clear();

    // Buy Side
    if (incoming_order.side == OrderSide::Buy) {
        auto it = asks_.begin();
        // the matching logic, iterating through the asks
        while (it != asks_.end() && incoming_order.quantity > 0 &&
               (incoming_order.type == OrderType::Market || incoming_order.price >= it->first)) {
            // get the list of orders at this price level
            auto& orders_at_price = it->second;
            // match against orders at this price level
            while (!orders_at_price.empty() && incoming_order.quantity > 0) {
                Order* existing_sell_order = orders_at_price.front(); // FIFO matching
                uint64_t trade_quantity = std::min(incoming_order.quantity, existing_sell_order->quantity);
                Trade new_trade = {
                    incoming_order.order_id,
                    existing_sell_order->order_id,
                    it->first,
                    trade_quantity
                };
                // Record the trade
                trades.push_back(new_trade);

                executed_trades_.push_back(new_trade);

                incoming_order.quantity -= trade_quantity;
                existing_sell_order->quantity -= trade_quantity;
                // If the existing order is fully filled, remove it from the book
                if (existing_sell_order->is_filled()) {
                    orders_at_price.pop_front();
                    order_lookup_[existing_sell_order->order_id] = nullptr;
                    order_pool_.return_order(existing_sell_order);
                }
            }
            // If no more orders at this price level, remove the price level
            if (orders_at_price.empty()) {
                it = asks_.erase(it);
            } else {
                ++it;
            }
        }
        // Sell Side
    } else {
        auto it = bids_.begin();
        // the matching logic, iterating through the bids
        while (it != bids_.end() && incoming_order.quantity > 0 &&
               (incoming_order.type == OrderType::Market || incoming_order.price <= it->first)) {
            // get the list of orders at this price level
            auto& orders_at_price = it->second;
            // match against orders at this price level
            while (!orders_at_price.empty() && incoming_order.quantity > 0) {
                Order* existing_buy_order = orders_at_price.front();
                uint64_t trade_quantity = std::min(incoming_order.quantity, existing_buy_order->quantity);
                // record the trade
                Trade new_trade = {existing_buy_order->order_id, incoming_order.order_id, it->first, trade_quantity};
                trades.push_back(new_trade);
                executed_trades_.push_back(new_trade);
                incoming_order.quantity -= trade_quantity;
                existing_buy_order->quantity -= trade_quantity;
                // if the existing order is fully filled, remove it from the book
                if (existing_buy_order->is_filled()) {
                    orders_at_price.pop_front();
                    order_lookup_[existing_buy_order->order_id] = nullptr;
                    order_pool_.return_order(existing_buy_order);
                }
            }
            // if no more orders at this price level, remove the price level
            if (orders_at_price.empty()) {
                it = bids_.erase(it);
            } else {
                ++it;
            }
        }
    }
}

bool OrderBook::can_fill_completely(const Order& order) const {
    // Walks the opposite side of the book and checks whether enough quantity is available
    // at the order's limit price (or better) to satisfy the full order — no book modifications.
    uint64_t available = 0;

    if (order.side == OrderSide::Buy) {
        // asks_ is sorted ascending: iterate while ask_price <= order.price
        for (const auto& [price, orders_at_price] : asks_) {
            if (price > order.price) break;
            for (const Order* o : orders_at_price) {
                available += o->quantity;
                if (available >= order.quantity) return true;
            }
        }
    } else {
        // bids_ is sorted descending: iterate while bid_price >= order.price
        for (const auto& [price, orders_at_price] : bids_) {
            if (price < order.price) break;
            for (const Order* o : orders_at_price) {
                available += o->quantity;
                if (available >= order.quantity) return true;
            }
        }
    }
    return false;
}

bool OrderBook::cancel_order(uint64_t order_id) {
    // direct index lookup — O(1), no hashing
    if (order_id >= order_lookup_.size() || order_lookup_[order_id] == nullptr) {
        return false;
    }

    Order* order_to_cancel = order_lookup_[order_id];
    order_lookup_[order_id] = nullptr;

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

    // return the order to the pool
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

// Formats a tick price as "$DDD.CC (TTTT ticks)"
static std::string fmt_tick(int32_t ticks) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "$%d.%02d (%d ticks)", ticks / 100, ticks % 100, ticks);
    return buf;
}

std::ostream& operator<<(std::ostream& os, const OrderBook& book) {
    os << "Order Book State (1 tick = $0.01):\n";

    os << "  Asks (best first):\n";
    // asks_ is sorted ascending — reverse-print so best ask is at the bottom (visually natural)
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
