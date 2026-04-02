# Low Latency Order Book in C++

basically a simulated order book written in C++ — the kind of thing you'd find at the core of a trading exchange. the goal was to make it as fast as possible and learn how low latency systems actually work.

currently sitting at around 10.6 million operations per second on my machine, which i'm pretty happy with.

## what it does

- maintains a live order book with a bid side and an ask side
- matches incoming orders against resting ones (price-time priority, so FIFO within each price level)
- supports limit, market, IoC (immediate-or-cancel), and FOK (fill-or-kill) order types
- cancel orders by ID
- uses a memory pool for orders so we're not calling malloc on every single order

## how the matching works

when an order comes in, it gets matched against the opposite side of the book at the best available price first, then the next best, and so on until it's either fully filled or there's nothing left to match against. what happens to any remaining quantity depends on the order type — limit orders rest in the book, market and IoC orders get cancelled, FOK orders get killed entirely if they can't be completely filled.

## performance stuff

a bunch of optimisations have been made to get to where it is now — see OPTIMISATIONS.md for the full breakdown with benchmark numbers for each change. the main ones so far:

- replaced chrono timestamps with a simple sequence counter (chrono was getting called on every single order and trade)
- reusable trade buffer so we're not allocating a new vector on every process_order call
- pre-reserved the trade history vector so it doesn't reallocate mid-benchmark

next up is replacing the std::map for price levels with a flat array, which should be the biggest jump yet.

## to do

- flat array for price levels (in progress)
- look at replacing std::deque at each price level with something faster
- replace std::unordered_map for order lookups
- multithreading eventually, once the single-threaded performance is maxed out
