#pragma once

// Trade.h - defines what a trade looks like
// a trade gets created inside match_and_fill (OrderBook.cpp) every time two orders cross.
// trades get stored in trades_buf_ and executed_trades_ on the orderbook,
// and returned to the caller via a span in ProcessOrderResult.

#include <cstdint>

// a trade - just holds the info about what happened when two orders matched
// no timestamp anymore - it was never actually used and calling chrono on every trade was slow

struct Trade {
    uint64_t buyer_order_id;
    uint64_t seller_order_id;
    int32_t price;     // price in ticks at which the trade happened (1 tick = $0.01)
    uint64_t quantity;
};
