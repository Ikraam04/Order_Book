#include "OrderBook.h"
#include <algorithm>
#include <iostream>

//constructor
OrderBook::OrderBook() : order_pool_(2'500'000) { // Increased pool size as needed
}

// destructor
OrderBook::~OrderBook() {
}

ProcessOrderResult OrderBook::process_order(Order new_order_data) {
    ProcessOrderResult result; // Create the result object

    Order* incoming_order = order_pool_.get_order();

    incoming_order->order_id = next_order_id_++;
    incoming_order->side = new_order_data.side;
    incoming_order->type = new_order_data.type;
    incoming_order->price = new_order_data.price;
    incoming_order->quantity = new_order_data.quantity;
    incoming_order->timestamp = std::chrono::high_resolution_clock::now();

    // match and fill the incoming order
    result.trades = match_and_fill(*incoming_order);
    // if the order is not fully filled and is a limit order, add it to the book
    if (!incoming_order->is_filled() && incoming_order->type == OrderType::Limit) {
        if (incoming_order->side == OrderSide::Buy) {
            bids_[incoming_order->price].push_back(incoming_order);
        } else { // sell Side
            asks_[incoming_order->price].push_back(incoming_order);
        }
        orders_by_id_[incoming_order->order_id] = incoming_order;

        // If the order was added to the book, set its ID in the result
        result.new_order_id = incoming_order->order_id;
    } else {
        order_pool_.return_order(incoming_order);
    }

    return result; // return result
}

std::vector<Trade> OrderBook::match_and_fill(Order& incoming_order) {
    std::vector<Trade> trades;

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
                // Record the trade
                trades.push_back({incoming_order.order_id, existing_sell_order->order_id, it->first, trade_quantity, std::chrono::high_resolution_clock::now()});

                incoming_order.quantity -= trade_quantity;
                existing_sell_order->quantity -= trade_quantity;
                // If the existing order is fully filled, remove it from the book
                if (existing_sell_order->is_filled()) {
                    orders_at_price.pop_front();
                    orders_by_id_.erase(existing_sell_order->order_id);
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
                trades.push_back({existing_buy_order->order_id, incoming_order.order_id, it->first, trade_quantity, std::chrono::high_resolution_clock::now()});

                incoming_order.quantity -= trade_quantity;
                existing_buy_order->quantity -= trade_quantity;
                // if the existing order is fully filled, remove it from the book
                if (existing_buy_order->is_filled()) {
                    orders_at_price.pop_front();
                    orders_by_id_.erase(existing_buy_order->order_id);
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
    return trades;
}

bool OrderBook::cancel_order(uint64_t order_id) {
    // Look up the order by id
    auto it = orders_by_id_.find(order_id);
    if (it == orders_by_id_.end()) {
        return false;
    }

    Order* order_to_cancel = it->second;
    orders_by_id_.erase(it);

    if (order_to_cancel->side == OrderSide::Buy) {
        auto& orders_at_price = bids_.at(order_to_cancel->price);
        // this is not the most efficient way, but it works for now
        auto order_it = std::find(orders_at_price.begin(), orders_at_price.end(), order_to_cancel);
        if (order_it != orders_at_price.end()) {
            orders_at_price.erase(order_it);
            if (orders_at_price.empty()) {
                bids_.erase(order_to_cancel->price);
            }
        }
    } else { // Sell Side
        auto& orders_at_price = asks_.at(order_to_cancel->price);
        auto order_it = std::find(orders_at_price.begin(), orders_at_price.end(), order_to_cancel);
        if (order_it != orders_at_price.end()) {
            orders_at_price.erase(order_it);
            if (orders_at_price.empty()) {
                asks_.erase(order_to_cancel->price);
            }
        }
    }

    // Return the cancelled order to the pool instead of calling 'delete'
    order_pool_.return_order(order_to_cancel);
    return true;
}

double OrderBook::get_best_bid() const {
    if (bids_.empty()) return 0.0;
    return bids_.begin()->first;
}

double OrderBook::get_best_ask() const {
    if (asks_.empty()) return 0.0;
    return asks_.begin()->first;
}

//make a pretty output of the order book
std::ostream& operator<<(std::ostream& os, const OrderBook& book) {
    os << "Order Book State:\n";

    os << "Bids:\n";
    for (const auto& [price, orders] : book.bids_) {
        os << "Price: " << price << ", Orders: ";
        for (const Order* order : orders) {
            os << "[ID: " << order->order_id << ", Qu: " << order->quantity << "] ";
        }
        os << "\n";
    }

    os << "Asks:\n";
    for (const auto& [price, orders] : book.asks_) {
        os << "Price: " << price << ", Orders: ";
        for (const Order* order : orders) {
            os << "[ID:" << order->order_id << ", Qu: " << order->quantity << "] ";
        }
        os << "\n";
    }

    return os;
}