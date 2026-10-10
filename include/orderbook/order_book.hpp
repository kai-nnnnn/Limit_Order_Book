#pragma once

#include <cstddef>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include "orderbook/order.hpp"
#include "orderbook/types.hpp"

namespace ob {

enum class Status {
    Accepted,
    RejectedDuplicateId,
    RejectedZeroQuantity,
    RejectedInvalidPrice,
};

struct AddResult {
    Status status = Status::Accepted;
    std::vector<Trade> trades;  // executions caused by this order, in order of occurrence
    Quantity filled = 0;        // total quantity executed immediately
    Quantity resting = 0;       // quantity left on the book (always 0 for market orders)

    bool accepted() const noexcept { return status == Status::Accepted; }
};

// Aggregated view of one price level, used for printing and testing.
struct LevelSummary {
    Price price;
    Quantity total_quantity;
    std::size_t order_count;

    friend bool operator==(const LevelSummary&, const LevelSummary&) = default;
};

// Single-instrument limit order book with price-time priority matching.
//
// Data structures (see docs/LEARNING.md for the reasoning):
//   bids_ : std::map<Price, Level, std::greater<>>  -> begin() is the HIGHEST bid
//   asks_ : std::map<Price, Level, std::less<>>     -> begin() is the LOWEST ask
//   Level : FIFO std::list<Order> + cached total quantity
//   index_: OrderId -> where the order lives, so cancel never scans the book
//
// Complexity (N = number of price levels, K = levels/orders consumed by a match):
//   add (no match)   O(log N)        cancel  O(log N) worst case, O(1) to find the order
//   best bid/ask     O(1)            match   O(K + log N)
class OrderBook {
public:
    // Limit order: execute against the opposite side while prices cross,
    // then rest any remainder on the book at `price`.
    AddResult add_limit(OrderId id, Side side, Price price, Quantity quantity);

    // Market order: execute against the opposite side at any price.
    // Any remainder that cannot be filled is discarded (never rests).
    AddResult add_market(OrderId id, Side side, Quantity quantity);

    // Remove a resting order. Returns false if the id is not on the book
    // (never existed, already fully filled, or already cancelled).
    bool cancel(OrderId id);

    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;

    // Total resting quantity at a price (0 if the level does not exist).
    Quantity volume_at(Side side, Price price) const;

    // Best `max_levels` levels of one side, best price first.
    std::vector<LevelSummary> depth(Side side, std::size_t max_levels = SIZE_MAX) const;

    bool contains(OrderId id) const { return index_.contains(id); }
    std::size_t order_count() const noexcept { return index_.size(); }
    bool empty() const noexcept { return index_.empty(); }

private:
    struct Level {
        std::list<Order> orders;  // front() = oldest = first to be filled
        Quantity total_quantity = 0;
    };

    using BidMap = std::map<Price, Level, std::greater<>>;
    using AskMap = std::map<Price, Level, std::less<>>;

    // Everything needed to find (and erase) a resting order in O(1)/O(log N).
    struct Locator {
        Side side;
        Price price;
        std::list<Order>::iterator it;
    };

    // Shared matching loop. `Book` is BidMap or AskMap (the side being consumed).
    // `limit` is empty for market orders.
    template <typename Book>
    void match(Book& book, OrderId taker_id, Side taker_side, std::optional<Price> limit,
               Quantity& remaining, std::vector<Trade>& trades);

    template <typename Book>
    void rest(Book& book, const Order& order);

    BidMap bids_;
    AskMap asks_;
    std::unordered_map<OrderId, Locator> index_;
};

}  // namespace ob
