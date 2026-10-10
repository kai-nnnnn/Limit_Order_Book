// Simple latency/throughput benchmark.
//
// Generates a realistic-ish random order flow (mostly limit orders near the
// mid price, some cancels, some market orders), then times each operation.
//
// Notes on measurement:
//   * The workload is generated BEFORE timing starts so RNG cost is excluded.
//   * Each op is timed with steady_clock; the clock call itself costs ~20-40 ns,
//     so very small numbers are an upper bound. Throughput is measured separately
//     with a single timer around the whole loop.
//   * Build in Release (-O2/-O3) or the numbers are meaningless.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "orderbook/order_book.hpp"

using namespace ob;
using Clock = std::chrono::steady_clock;

namespace {

enum class OpType { Limit, Market, Cancel };

struct Op {
    OpType type;
    OrderId id;
    Side side;
    Price price;
    Quantity qty;
};

std::vector<Op> make_workload(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::vector<Op> ops;
    ops.reserve(n);
    std::vector<OrderId> live;  // ids that might still be resting (for realistic cancels)
    OrderId next_id = 1;
    const Price mid = 10'000;

    for (std::size_t i = 0; i < n; ++i) {
        const int r = static_cast<int>(rng() % 100);
        const Side side = (rng() & 1) ? Side::Buy : Side::Sell;

        if (r < 65 || live.empty()) {
            // Limit order: mostly passive, offset 0..49 ticks from mid on its own side,
            // occasionally aggressive (crosses the spread).
            Price offset = static_cast<Price>(rng() % 50);
            const bool aggressive = (rng() % 10) == 0;
            Price px = side == Side::Buy ? mid - offset : mid + offset;
            if (aggressive) px = side == Side::Buy ? mid + 5 : mid - 5;
            const OrderId id = next_id++;
            ops.push_back({OpType::Limit, id, side, px, 1 + rng() % 100});
            live.push_back(id);
        } else if (r < 95) {
            // Cancel a random previously-submitted order (may already be filled).
            const std::size_t k = rng() % live.size();
            ops.push_back({OpType::Cancel, live[k], side, 0, 0});
            live[k] = live.back();
            live.pop_back();
        } else {
            ops.push_back({OpType::Market, next_id++, side, 0, 1 + rng() % 50});
        }
    }
    return ops;
}

void apply(OrderBook& book, const Op& op) {
    switch (op.type) {
        case OpType::Limit: book.add_limit(op.id, op.side, op.price, op.qty); break;
        case OpType::Market: book.add_market(op.id, op.side, op.qty); break;
        case OpType::Cancel: book.cancel(op.id); break;
    }
}

double percentile(const std::vector<std::int64_t>& sorted, double p) {
    const std::size_t idx = static_cast<std::size_t>(p * (sorted.size() - 1));
    return static_cast<double>(sorted[idx]);
}

}  // namespace

int main(int argc, char** argv) {
    const std::size_t n = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1'000'000;
    const auto ops = make_workload(n, 42);

    // 1) Throughput: one timer around the whole run.
    {
        OrderBook book;
        const auto start = Clock::now();
        for (const Op& op : ops) apply(book, op);
        const auto ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
        std::printf("ops:         %zu\n", n);
        std::printf("total time:  %.2f ms\n", ns / 1e6);
        std::printf("throughput:  %.2f M ops/s\n", n * 1e3 / ns);
        std::printf("mean:        %.1f ns/op\n", static_cast<double>(ns) / n);
        std::printf("final book:  %zu resting orders\n", book.order_count());
    }

    // 2) Latency distribution: time every op individually.
    {
        OrderBook book;
        std::vector<std::int64_t> lat;
        lat.reserve(n);
        for (const Op& op : ops) {
            const auto t0 = Clock::now();
            apply(book, op);
            const auto t1 = Clock::now();
            lat.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        }
        std::sort(lat.begin(), lat.end());
        std::printf("latency p50:   %.0f ns\n", percentile(lat, 0.50));
        std::printf("latency p90:   %.0f ns\n", percentile(lat, 0.90));
        std::printf("latency p99:   %.0f ns\n", percentile(lat, 0.99));
        std::printf("latency p99.9: %.0f ns\n", percentile(lat, 0.999));
        std::printf("latency max:   %lld ns\n", static_cast<long long>(lat.back()));
    }
    return 0;
}
