#pragma once

#include "orderbook/types.hpp"

namespace ob {

// A resting order on the book. `quantity` is the *remaining* (unfilled) quantity;
// it shrinks as the order is partially filled.
struct Order {
    OrderId id;
    Side side;
    Price price;
    Quantity quantity;
};

// One execution between a resting order (maker) and an incoming order (taker).
// Trades always happen at the maker's price: the maker set that price first,
// and the taker was willing to pay at least that much.
struct Trade {
    OrderId maker_id;
    OrderId taker_id;
    Side taker_side;  // BUY means the aggressor lifted an offer; SELL means it hit a bid
    Price price;
    Quantity quantity;

    friend bool operator==(const Trade&, const Trade&) = default;
};

}  // namespace ob
