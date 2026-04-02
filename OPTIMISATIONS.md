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