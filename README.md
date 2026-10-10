# Limit Order Book

A single-instrument **limit order book and matching engine** in modern C++ (C++20),
with price-time priority matching, unit + randomized differential tests, and a
latency benchmark.

## Features

- Limit orders (match while prices cross, rest the remainder)
- Market orders (sweep the opposite side; unfilled remainder is discarded)
- Cancel by order id
- **Price-time priority** (best price first; FIFO within a price level)
- Partial fills on both maker and taker side; trades execute at the maker's price
- Input validation (duplicate id, zero quantity, non-positive price)
- Command-line driver for replaying order scripts

## Build & run

Requires CMake ≥ 3.20 and a C++20 compiler (Apple Clang 15+, GCC 11+, Clang 14+).
GoogleTest is downloaded automatically at configure time.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

./build/orderbook_tests                 # run all tests
./build/lob_cli examples/lesson1.txt    # replay a scenario
./build/lob_bench 1000000               # benchmark with 1M operations
```

CLI commands (one per line):

```
LIMIT  BUY|SELL <id> <price> <qty>
MARKET BUY|SELL <id> <qty>
CANCEL <id>
PRINT
```

## Design

```
OrderBook
├── bids_  : std::map<Price, Level, std::greater<>>   begin() = best (highest) bid
├── asks_  : std::map<Price, Level, std::less<>>      begin() = best (lowest) ask
└── index_ : std::unordered_map<OrderId, Locator>     id -> {side, price, list iterator}

Level = { std::list<Order> orders (FIFO), Quantity total_quantity }
```

| Operation              | Complexity                      |
|------------------------|---------------------------------|
| Add (no match)         | O(log L)                        |
| Match                  | O(K + log L), K = orders filled |
| Cancel                 | O(1) lookup + O(log L) level    |
| Best bid / ask         | O(1)                            |

(L = number of price levels on one side.)

Key decisions:

- **Integer prices (ticks)**, never `double`: prices must compare exactly.
- **Ordered map per side** so the best price is always `begin()`.
- **`std::list` per level** gives FIFO order *and* iterators that stay valid
  while other orders are inserted/removed, so the id index can store an
  iterator and cancel without scanning.
- **Empty levels are erased immediately**, keeping `begin()` meaningful.

## Testing

- `tests/order_book_test.cpp` — 21 scenario tests: priority rules, partial fills,
  multi-level sweeps, market orders, cancels, validation.
- `tests/randomized_test.cpp` — **differential test**: 20 seeds × 2,000 random
  operations are applied to both this book and a brute-force reference
  implementation; trades and full book depth must match after every step,
  and the book must never be crossed.
- Clean under AddressSanitizer and UndefinedBehaviorSanitizer.

## Benchmark

1,000,000 operations (65% limit, 30% cancel, 5% market; prices clustered near mid),
Release build, single thread, median of 5 runs:

| Metric      | Value          |
|-------------|----------------|
| Throughput  | ~9.4 M ops/s   |
| Mean        | ~107 ns/op     |
| p50         | 42–83 ns       |
| p90         | ~166 ns        |
| p99         | ~333 ns        |
| p99.9       | ~625 ns        |
| Max         | ~4.5 ms        |

_MacBook Air (Apple M3, 16 GB), Apple Clang 21, macOS 26.
Apple Silicon's timer resolution is ~42 ns, so per-op latencies are quantized to
multiples of it and include clock overhead; throughput is timed over the whole run._

## Roadmap

- [ ] Modify order (quantity down keeps priority; price change loses it)
- [ ] IOC / FOK time-in-force
- [ ] Multiple instruments
- [ ] Replace `std::map`/`std::list` with cache-friendly structures + object pool;
      compare before/after latency
- [ ] Market data replay (e.g. ITCH) to validate against real order flow
