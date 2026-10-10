#pragma once

#include <cstdint>
#include <string_view>

namespace ob {

// Prices are integer "ticks", never floating point.
// e.g. with a tick size of $0.01, the price $101.25 is stored as 10125.
// Floating point cannot represent most decimal prices exactly (0.1 + 0.2 != 0.3),
// and two orders at "the same" price must compare equal, so integers are required.
using Price = std::int64_t;
using Quantity = std::uint64_t;
using OrderId = std::uint64_t;

enum class Side : std::uint8_t { Buy, Sell };

constexpr Side opposite(Side s) noexcept {
    return s == Side::Buy ? Side::Sell : Side::Buy;
}

constexpr std::string_view to_string(Side s) noexcept {
    return s == Side::Buy ? "BUY" : "SELL";
}

}  // namespace ob
