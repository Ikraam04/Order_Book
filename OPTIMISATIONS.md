# order book optimisation log

## baseline structure

before any changes, here's what the code was doing:

- price levels were stored in `std::map<int32_t, std::deque<Order*>>` - a red-black tree, so every insert/lookup/erase is O(log n) and the nodes are scattered around in heap memory (bad for cache)
- the queue at each price level was a `std::deque` - internally uses fixed-size heap blocks so the order pointers aren't contiguous, and `pop_front` involves block bookkeeping
- order lookup for cancels used `std::unordered_map` - hashing + pointer chasing through linked list nodes on every find/erase
- `chrono::now()` was being called once per order and once per trade - on x86 this is a serialising instruction that can cost 20-50ns each time
- `match_and_fill` returned a `std::vector<Trade>` by value - heap allocation on every single process_order call
- `executed_trades_` had no reserved capacity so it kept reallocating as it grew

## baseline numbers

```
throughput:  7,531,045 ops/sec
mean:        172.843 ns
p50:         100 ns
p99:         900 ns
p99.9:       2000 ns
max:         757700 ns
```

---

## opt 1 - removed chrono from the hot path

`chrono::now()` was being called on every order and every trade. on x86 this involves a serialising instruction (`RDTSC`) which can stall the pipeline and costs 20-50ns per call. at millions of calls per second that adds up fast.

the thing is, timestamps weren't actually being used for anything - fifo ordering was already handled by the queue position, and nothing in the codebase was reading `Trade::timestamp`. so i just removed them entirely.

replaced `Order::timestamp` with a `uint64_t seq` counter (same value as order_id, basically free to set) and dropped `Trade::timestamp` completely.

```
throughput:  9,318,132 ops/sec  (+23.7%)
mean:        136.012 ns         (-21.3%)
p99.9:       1300 ns            (-35%)
max:         575900 ns          (-24%)
```

---

## opt 2 - stopped allocating a new vector on every process_order call

`match_and_fill` used to create a fresh `std::vector<Trade>` every time it ran - that's a malloc + free on literally every order processed. even orders that don't match anything still paid this cost.

added `trades_buf_` as a reusable member vector on the orderbook. `match_and_fill` just calls `clear()` on it at the start of each call - that resets the size to 0 but keeps the memory, so no allocation happens.

also pre-reserved `executed_trades_` to 2.5M entries upfront so it never needs to reallocate mid-benchmark. that's what was causing the big max latency spikes - occasional doubling reallocations.

```
throughput:  10,650,970 ops/sec  (+14.3%)
mean:        119.239 ns          (-12.3%)
p99.9:       1200 ns             (-7.7%)
max:         276600 ns           (-52%)
```

---

## opt 3 - replaced std::deque with a vector + head index at each price level

`std::deque` internally manages multiple heap-allocated blocks. even though `pop_front` is O(1), it still involves the block structure and the order pointers aren't contiguous in memory.

replaced it with a `PriceLevel` struct - just a `std::vector<Order*>` and a `head` index. `pop_front()` is literally `head++`. all the order pointers sit in one contiguous allocation so the cpu prefetcher can do its job.

when a level fully empties, the vector gets cleared and the map entry is removed - same as before, no unbounded memory growth.

```
throughput:  12,753,696 ops/sec  (+19.7%)
mean:        101.344 ns          (-15%)
p99:         800 ns              (-11.1%)
p99.9:       1100 ns             (-8.3%)
max:         119000 ns           (-57%)
```

---

## opt 4 - replaced unordered_map with a flat array for order lookup

`std::unordered_map` does a hash on every find/erase, then chases a pointer to a heap-allocated node to get the actual value. that's at minimum one cache miss per cancel/match.

since order IDs are sequential integers starting at 1, we can just use them directly as array indices. `order_lookup_[order_id]` is just pointer + offset, no hashing, no indirection.

costs about 20MB of memory (2.5M pointers x 8 bytes) but that's fine. throughput gain was smaller here because this path only gets hit on matched orders and cancels, not every op. but the latency tail got noticeably better.

```
throughput:  12,991,892 ops/sec  (+1.9%)
mean:        89.442 ns           (-11.7%)
p99.9:       1000 ns             (-9.1%)
max:         64700 ns            (-45.7%)
```

---

## opt 5 - eliminated the result.trades vector copy using std::span

even with the reusable `trades_buf_` from opt 2, `match_and_fill` was still returning `std::vector<Trade>` by value. that meant copying all the trades out of `trades_buf_` into a new temporary vector on every call, then moving that into `result.trades`. the copy was the expensive bit.

changed `result.trades` from `std::vector<Trade>` to `std::span<const Trade>`. a span is just a pointer and a length - two words. setting `result.trades = trades_buf_` is basically free, it just points at the existing buffer.

the tradeoff is the span goes stale after the next `process_order` call (because `trades_buf_` gets cleared). but in any normal sequential loop you'd have processed the result by then anyway.

the max latency improvement here is wild - large matching ops that generated lots of trades were paying a big copy cost each time. now that cost is zero.

```
throughput:  15,308,843 ops/sec  (+17.8%)
mean:        75.597 ns           (-15.5%)
p99.9:       900 ns              (-10%)
max:         23500 ns            (-63.7%)
```

---

## overall from baseline

| metric | baseline | final | delta |
|---|---|---|---|
| throughput | 7,531,045 ops/sec | 15,308,843 ops/sec | +103% |
| mean | 172.843 ns | 75.597 ns | -56% |
| p99.9 | 2000 ns | 900 ns | -55% |
| max | 757700 ns | 23500 ns | -97% |
