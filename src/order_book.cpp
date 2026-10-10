#include "orderbook/order_book.hpp"

#include <algorithm>

namespace ob {

namespace {

// Does an incoming order with limit price `limit` cross a resting level at `level_price`?
//   A BUY at 101 can trade with asks at <= 101.
//   A SELL at 99 can trade with bids at >= 99.
bool crosses(Side taker_side, Price limit, Price level_price) {
    return taker_side == Side::Buy ? limit >= level_price : limit <= level_price;
}

}  // namespace

template <typename Book>
void OrderBook::match(Book& book, OrderId taker_id, Side taker_side, std::optional<Price> limit,
                      Quantity& remaining, std::vector<Trade>& trades) {
    // Price priority: always work on the best level first (book.begin()).
    while (remaining > 0 && !book.empty()) {
        auto level_it = book.begin();
        const Price level_price = level_it->first;

        if (limit && !crosses(taker_side, *limit, level_price)) {
            break;  // best opposite price is worse than our limit -> nothing more can trade
        }

        Level& level = level_it->second;

        // Time priority: inside a level, fill the oldest order (front of the FIFO) first.
        while (remaining > 0 && !level.orders.empty()) {
            Order& maker = level.orders.front();
            const Quantity fill = std::min(remaining, maker.quantity);

            trades.push_back(Trade{maker.id, taker_id, taker_side, level_price, fill});

            remaining -= fill;
            maker.quantity -= fill;
            level.total_quantity -= fill;

            if (maker.quantity == 0) {
                index_.erase(maker.id);
                level.orders.pop_front();
            }
        }

        // Never leave an empty level behind: best_bid()/best_ask() rely on
        // begin() always being a level that actually has orders.
        if (level.orders.empty()) {
            book.erase(level_it);
        }
    }
}

template <typename Book>
void OrderBook::rest(Book& book, const Order& order) {
    // operator[] creates the level if it does not exist yet: O(log N).
    Level& level = book[order.price];
    level.orders.push_back(order);  // newest goes to the back -> time priority
    level.total_quantity += order.quantity;

    // std::list iterators stay valid until that element is erased,
    // which is exactly why we can store one in the index.
    index_.emplace(order.id, Locator{order.side, order.price, std::prev(level.orders.end())});
}

AddResult OrderBook::add_limit(OrderId id, Side side, Price price, Quantity quantity) {
    AddResult result;
    if (quantity == 0) {
        result.status = Status::RejectedZeroQuantity;
        return result;
    }
    if (price <= 0) {
        result.status = Status::RejectedInvalidPrice;
        return result;
    }
    if (index_.contains(id)) {
        result.status = Status::RejectedDuplicateId;
        return result;
    }

    Quantity remaining = quantity;
    if (side == Side::Buy) {
        match(asks_, id, side, price, remaining, result.trades);
    } else {
        match(bids_, id, side, price, remaining, result.trades);
    }

    result.filled = quantity - remaining;

    if (remaining > 0) {
        const Order order{id, side, price, remaining};
        if (side == Side::Buy) {
            rest(bids_, order);
        } else {
            rest(asks_, order);
        }
        result.resting = remaining;
    }
    return result;
}

AddResult OrderBook::add_market(OrderId id, Side side, Quantity quantity) {
    AddResult result;
    if (quantity == 0) {
        result.status = Status::RejectedZeroQuantity;
        return result;
    }
    if (index_.contains(id)) {
        result.status = Status::RejectedDuplicateId;
        return result;
    }

    Quantity remaining = quantity;
    if (side == Side::Buy) {
        match(asks_, id, side, std::nullopt, remaining, result.trades);
    } else {
        match(bids_, id, side, std::nullopt, remaining, result.trades);
    }

    result.filled = quantity - remaining;
    result.resting = 0;  // unfilled remainder of a market order is dropped
    return result;
}

bool OrderBook::cancel(OrderId id) {
    auto found = index_.find(id);
    if (found == index_.end()) {
        return false;
    }
    const Locator loc = found->second;

    auto erase_from = [&](auto& book) {
        auto level_it = book.find(loc.price);  // O(log N)
        Level& level = level_it->second;
        level.total_quantity -= loc.it->quantity;
        level.orders.erase(loc.it);  // O(1): we already hold the iterator
        if (level.orders.empty()) {
            book.erase(level_it);
        }
    };

    if (loc.side == Side::Buy) {
        erase_from(bids_);
    } else {
        erase_from(asks_);
    }
    index_.erase(found);
    return true;
}

std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

Quantity OrderBook::volume_at(Side side, Price price) const {
    auto lookup = [price](const auto& book) -> Quantity {
        auto it = book.find(price);
        return it == book.end() ? 0 : it->second.total_quantity;
    };
    return side == Side::Buy ? lookup(bids_) : lookup(asks_);
}

std::vector<LevelSummary> OrderBook::depth(Side side, std::size_t max_levels) const {
    std::vector<LevelSummary> out;
    auto collect = [&](const auto& book) {
        for (const auto& [price, level] : book) {
            if (out.size() >= max_levels) break;
            out.push_back(LevelSummary{price, level.total_quantity, level.orders.size()});
        }
    };
    if (side == Side::Buy) {
        collect(bids_);
    } else {
        collect(asks_);
    }
    return out;
}

}  // namespace ob
