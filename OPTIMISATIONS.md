# Order Book Optimisation Log

## Current Structure (Baseline)

### Data Structures
- **Price levels**: `std::map<int32_t, std::deque<Order*>>` for both bids and asks. Red-black tree, so every insert/lookup/erase is O(log n) with pointer chasing and poor cache locality.
- **Order queue at each level**: `std::deque<Order*>` — double-indirection block structure, slow `pop_front` under load.
- **Order lookup by ID**: `std::unordered_map<uint64_t, Order*>` — used for cancels, hash table with chaining so cache-unfriendly on collision.
- **Memory**: `OrderPool` — pre-allocated flat `std::vector<Order>` (2.5M slots) with a `std::vector<Order*>` free list. Returns orders via `pop_back`/`push_back` so it's O(1), but the free list itself is a separate heap allocation.

### Trade Storage
- Every trade is recorded twice: once in the local `std::vector<Trade>` returned from `match_and_fill`, and once appended to `executed_trades_` (a member vector that grows unboundedly).
- `ProcessOrderResult` owns a `std::vector<Trade>` by value — this means a heap allocation on every `process_order` call.

### Timestamps
- `std::chrono::high_resolution_clock::now()` is called once per incoming order (in `process_order`) and once per trade (inside `match_and_fill`). On x86 this can be 20–50 ns per call and can serialise the CPU pipeline.

### Other notes
- Prices are integer ticks (int32_t, 1 tick = $0.01). Already faster than floating point comparison.
- The benchmark price range is clamped to [9000, 11000] ticks ($90–$110), which means only ~2000 distinct price levels are ever active — relevant for any array-based replacement of the map.
- Order types: 40% Limit, 20% Market, 20% IoC, 20% FOK. FOK does a full dry-run scan (`can_fill_completely`) before matching, which walks the book twice on a fill.
- Cancel rate: ~20% of ops are cancels. Cancel uses `std::find` (O(n) scan) on the deque at the target price level.

---

**Build**: Release (MSVC, no flags noted)

### Throughput Benchmark (add + cancel, 2.5M ops)
```
Orders submitted:         1999211
FOK killed:               321731
IoC partial fills:        322130
Cancels (successful):     201
Cancels (already filled): 500588
Total operations:         2500000
Time:                     0.331959 s
Throughput:               7,531,045 ops/sec
```

### Latency Benchmark (add-only, 200k ops)
```
mean:   172.843 ns
p50:    100 ns
p99:    900 ns
p99.9:  2000 ns
max:    757700 ns
```

---

## Optimisation 1 — Remove chrono from hot path (2026-04-02)

### What changed
- `Order::timestamp` (`std::chrono::high_resolution_clock::time_point`) replaced with `uint64_t seq`, assigned directly from `order_id` (already computed, essentially free).
- `Trade::timestamp` removed entirely — nothing in the codebase was reading it.
- All `chrono::now()` calls removed from `process_order` and `match_and_fill`. The benchmark timing in `main.cpp` still uses chrono legitimately.
- `<chrono>` include dropped from `Order.h`, `Order.cpp`, and `Trade.h`.

### Why it helped
`high_resolution_clock::now()` on x86 involves a syscall or serialising instruction (`CPUID`/`RDTSC`). At 2–3 calls per order processed, that's easily 50–150 ns of pure overhead per op. Replacing it with a counter increment costs nothing — the value was already sitting in a register.

Time priority (FIFO within a price level) was never actually using the timestamp for comparisons anyway — the queue order already enforced arrival order. So this was all cost, no benefit.

### Current structure after this change
Same as baseline except:
- `Order` has `uint64_t seq` instead of a chrono timestamp
- `Trade` has no timestamp field

### Results — 2026-04-02
**Build**: Release (MSVC)

#### Throughput Benchmark (add + cancel, 2.5M ops)
```
Orders submitted:         1999211
FOK killed:               321731
IoC partial fills:        322130
Cancels (successful):     201
Cancels (already filled): 500588
Total operations:         2500000
Time:                     0.268294 s
Throughput:               9,318,132 ops/sec
```

#### Latency Benchmark (add-only, 200k ops)
```
mean:   136.012 ns
p50:    100 ns
p99:    900 ns
p99.9:  1300 ns
max:    575900 ns
```

#### vs Baseline
| Metric | Baseline | After | Delta |
|---|---|---|---|
| Throughput | 7,531,045 ops/sec | 9,318,132 ops/sec | +23.7% |
| Mean latency | 172.843 ns | 136.012 ns | -21.3% |
| p99.9 | 2000 ns | 1300 ns | -35.0% |
| Max | 757,700 ns | 575,900 ns | -24.0% |

---

## Optimisation 2 — Eliminate per-call heap allocations for trades (2026-04-02)

### What changed
- Added `trades_buf_` (`std::vector<Trade>`) as a member of `OrderBook`. `match_and_fill` now clears and fills this reusable buffer instead of constructing a fresh `std::vector<Trade>` on every call.
- `process_order` moves (not copies) the result out of the temporary into `result.trades`.
- `executed_trades_.reserve(2'500'000)` added to the constructor — pre-allocates the full trade history buffer upfront so it never reallocates mid-benchmark.
- `trades_buf_.reserve(64)` seeds it with a small initial buffer so the first few calls don't trigger allocations.

### Why it helped
Every call to `process_order` previously caused `match_and_fill` to construct a new `std::vector<Trade>` — that's a `malloc`/`free` on every single op, even when no trades occur. At 2M+ ops/sec this adds up fast. The reusable buffer pays the allocation cost once, then just calls `clear()` (which resets the size to 0 but keeps the memory) on every subsequent call.

`executed_trades_` was also triggering periodic reallocations (doubling the buffer) throughout the benchmark run, causing latency spikes. Reserving upfront flattens those out — visible in the max latency dropping from 575,900 ns to 276,600 ns.

### Current structure after this change
Same as after Optimisation 1, plus:
- `OrderBook` owns `trades_buf_` — a reusable working buffer for trades, cleared each call
- `executed_trades_` is pre-reserved to 2.5M entries

### Results — 2026-04-02
**Build**: Release (MSVC)

#### Throughput Benchmark (add + cancel, 2.5M ops)
```
Orders submitted:         1999211
FOK killed:               321731
IoC partial fills:        322130
Cancels (successful):     201
Cancels (already filled): 500588
Total operations:         2500000
Time:                     0.23472 s
Throughput:               10,650,970 ops/sec
```

#### Latency Benchmark (add-only, 200k ops)
```
mean:   119.239 ns
p50:    100 ns
p99:    900 ns
p99.9:  1200 ns
max:    276600 ns
```

#### vs Previous (after Opt 1)
| Metric | Opt 1 | Opt 2 | Delta |
|---|---|---|---|
| Throughput | 9,318,132 ops/sec | 10,650,970 ops/sec | +14.3% |
| Mean latency | 136.012 ns | 119.239 ns | -12.3% |
| p99.9 | 1300 ns | 1200 ns | -7.7% |
| Max | 575,900 ns | 276,600 ns | -52.0% |

---

## Optimisation 3 — Replace std::deque with vector + head index at each price level (2026-04-03)

### What changed
- Introduced a `PriceLevel` struct to replace `std::deque<Order*>` at each price level in `bids_` and `asks_`.
- `PriceLevel` wraps a `std::vector<Order*>` and a `size_t head` index. `pop_front()` just increments `head` — no memory movement, no deallocation.
- Exposes the same interface (`front()`, `pop_front()`, `push_back()`, `empty()`, iterators, `erase()`) so the matching loop and cancel logic needed no changes.
- Removed `#include <deque>`.

### Why it helped
`std::deque` internally manages a set of fixed-size heap-allocated blocks. Even though `pop_front()` is O(1), it involves the deque's block bookkeeping and the Order* pointers aren't in one contiguous chunk — they're scattered across blocks. Every `front()` call during matching follows an extra level of indirection.

`PriceLevel` stores all Order* pointers in a single contiguous vector. `pop_front` is a single integer increment. The CPU's prefetcher can stride through the active entries efficiently. When the level fully empties, the vector is cleared and the map entry is erased as before, so there's no unbounded memory growth.

### Current structure after this change
Same as after Optimisation 2, except:
- `bids_` and `asks_` are `std::map<int32_t, PriceLevel>` instead of `std::map<int32_t, std::deque<Order*>>`
- Each `PriceLevel` is a vector + head index — contiguous, no block indirection

### Results — 2026-04-03
**Build**: Release (MSVC)

#### Throughput Benchmark (add + cancel, 2.5M ops)
```
Orders submitted:         1999211
FOK killed:               321731
IoC partial fills:        322130
Cancels (successful):     201
Cancels (already filled): 500588
Total operations:         2500000
Time:                     0.196022 s
Throughput:               12,753,696 ops/sec
```

#### Latency Benchmark (add-only, 200k ops)
```
mean:   101.344 ns
p50:    100 ns
p99:    800 ns
p99.9:  1100 ns
max:    119000 ns
```

#### vs Previous (after Opt 2)
| Metric | Opt 2 | Opt 3 | Delta |
|---|---|---|---|
| Throughput | 10,650,970 ops/sec | 12,753,696 ops/sec | +19.7% |
| Mean latency | 119.239 ns | 101.344 ns | -15.0% |
| p99 | 900 ns | 800 ns | -11.1% |
| p99.9 | 1200 ns | 1100 ns | -8.3% |
| Max | 276,600 ns | 119,000 ns | -57.0% |

---

## Optimisation 4 — Replace unordered_map with flat vector for order lookup (2026-04-03)

### What changed
- Removed `std::unordered_map<uint64_t, Order*> orders_by_id_`.
- Replaced with `std::vector<Order*> order_lookup_` sized to 2,500,001 (matching the pool), all initialised to `nullptr`.
- Lookup is now `order_lookup_[order_id]` — a direct array index.
- On insert: `order_lookup_[id] = pointer`. On fill/cancel: `order_lookup_[id] = nullptr`.
- Removed `#include <unordered_map>`.

### Why it helped
`std::unordered_map` uses separate chaining — each bucket is a linked list of heap-allocated nodes. Every `find` or `erase` involves computing a hash, indexing into the bucket array, then following a pointer to a node somewhere in heap memory. That's at minimum one cache miss per lookup.

Since order IDs are sequential integers starting at 1, a direct array index is just a pointer + offset — O(1) with no hashing, no chaining, and the lookup lands in a predictable memory location. The tradeoff is ~20MB of memory for the vector (2.5M pointers × 8 bytes).

The throughput gain is smaller than previous optimisations because this path is only hit on matched orders and cancels, not every operation. The latency improvement is more visible — the hash map was causing occasional expensive lookups that showed up in the tail.

### Current structure after this change
Same as after Optimisation 3, except:
- Order lookup is a flat `std::vector<Order*>` indexed directly by `order_id`
- No hash map, no heap-allocated nodes

### Results — 2026-04-03
**Build**: Release (MSVC)

#### Throughput Benchmark (add + cancel, 2.5M ops)
```
Orders submitted:         1999211
FOK killed:               321731
IoC partial fills:        322130
Cancels (successful):     201
Cancels (already filled): 500588
Total operations:         2500000
Time:                     0.192428 s
Throughput:               12,991,892 ops/sec
```

#### Latency Benchmark (add-only, 200k ops)
```
mean:   89.442 ns
p50:    100 ns
p99:    800 ns
p99.9:  1000 ns
max:    64700 ns
```

#### vs Previous (after Opt 3)
| Metric | Opt 3 | Opt 4 | Delta |
|---|---|---|---|
| Throughput | 12,753,696 ops/sec | 12,991,892 ops/sec | +1.9% |
| Mean latency | 101.344 ns | 89.442 ns | -11.7% |
| p99.9 | 1100 ns | 1000 ns | -9.1% |
| Max | 119,000 ns | 64,700 ns | -45.7% |

---

## Optimisation 5 — Eliminate result.trades vector copy via std::span (2026-04-03)

### What changed
- `ProcessOrderResult::trades` changed from `std::vector<Trade>` to `std::span<const Trade>`.
- `match_and_fill` changed from returning `std::vector<Trade>` by value to `void` — it fills `trades_buf_` directly and returns nothing.
- `process_order` now sets `result.trades = trades_buf_` which constructs a span (just a pointer + size, no allocation).
- Added `#include <span>`.

### Why it helped
Previously, every `process_order` call did three things with trades: fill `trades_buf_`, copy it into a temporary return value, then move that into `result.trades`. Even with the move, the copy from `trades_buf_` into the temporary was a full vector copy — allocating new memory and copying all Trade objects.

A `std::span` is just a pointer and a length — two words. Setting `result.trades = trades_buf_` is effectively free. No allocation, no copying. The tradeoff is that the span is only valid until the next `process_order` call, since `trades_buf_` gets cleared at the start of each call. For sequential use (which covers both the benchmark and any normal trading loop) this is fine.

The max latency improvement (-63.7%) is the most striking result — large matching operations that generated many trades were paying the biggest copy cost, and those are now completely free.

### Current structure after this change
Same as after Optimisation 4, except:
- `ProcessOrderResult::trades` is `std::span<const Trade>` — a zero-copy view into `trades_buf_`
- `match_and_fill` is `void`, writes directly into `trades_buf_`

### Results — 2026-04-03
**Build**: Release (MSVC)

#### Throughput Benchmark (add + cancel, 2.5M ops)
```
Orders submitted:         1999211
FOK killed:               321731
IoC partial fills:        322130
Cancels (successful):     201
Cancels (already filled): 500588
Total operations:         2500000
Time:                     0.163304 s
Throughput:               15,308,843 ops/sec
```

#### Latency Benchmark (add-only, 200k ops)
```
mean:   75.597 ns
p50:    100 ns
p99:    800 ns
p99.9:  900 ns
max:    23500 ns
```

#### vs Previous (after Opt 4)
| Metric | Opt 4 | Opt 5 | Delta |
|---|---|---|---|
| Throughput | 12,991,892 ops/sec | 15,308,843 ops/sec | +17.8% |
| Mean latency | 89.442 ns | 75.597 ns | -15.5% |
| p99.9 | 1000 ns | 900 ns | -10.0% |
| Max | 64,700 ns | 23,500 ns | -63.7% |

---