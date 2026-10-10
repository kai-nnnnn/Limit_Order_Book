// Randomized differential test.
//
// ReferenceBook is a deliberately naive implementation: one flat vector of orders,
// and every operation is a brute-force scan. It is slow but easy to verify by eye.
// We feed the same random stream of operations to both books and require that
// they produce identical trades and identical book state at every step.
// This catches bugs that hand-written cases miss (stale index entries, empty
// levels left behind, wrong FIFO order after cancels, ...).

#include <gtest/gtest.h>

#include <algorithm>
#include <random>

#include "orderbook/order_book.hpp"

using namespace ob;

namespace {

class ReferenceBook {
public:
    std::vector<Trade> add(OrderId id, Side side, std::optional<Price> limit, Quantity qty) {
        std::vector<Trade> trades;
        while (qty > 0) {
            // Find the best opposite order: best price, then earliest sequence number.
            int best = -1;
            for (int i = 0; i < static_cast<int>(orders_.size()); ++i) {
                const Entry& e = orders_[i];
                if (e.order.side == side) continue;
                if (limit) {
                    bool ok = side == Side::Buy ? e.order.price <= *limit : e.order.price >= *limit;
                    if (!ok) continue;
                }
                if (best == -1 || better(e, orders_[best])) best = i;
            }
            if (best == -1) break;
            Entry& maker = orders_[best];
            Quantity fill = std::min(qty, maker.order.quantity);
            trades.push_back(Trade{maker.order.id, id, side, maker.order.price, fill});
            qty -= fill;
            maker.order.quantity -= fill;
            if (maker.order.quantity == 0) orders_.erase(orders_.begin() + best);
        }
        if (qty > 0 && limit) {
            orders_.push_back(Entry{Order{id, side, *limit, qty}, seq_++});
        }
        return trades;
    }

    bool cancel(OrderId id) {
        auto it = std::find_if(orders_.begin(), orders_.end(),
                               [id](const Entry& e) { return e.order.id == id; });
        if (it == orders_.end()) return false;
        orders_.erase(it);
        return true;
    }

    std::vector<LevelSummary> depth(Side side) const {
        std::map<Price, LevelSummary> levels;
        for (const Entry& e : orders_) {
            if (e.order.side != side) continue;
            auto& lvl = levels[e.order.price];
            lvl.price = e.order.price;
            lvl.total_quantity += e.order.quantity;
            lvl.order_count += 1;
        }
        std::vector<LevelSummary> out;
        for (auto& [p, l] : levels) out.push_back(l);
        if (side == Side::Buy) std::reverse(out.begin(), out.end());  // best (highest) first
        return out;
    }

    std::vector<OrderId> ids() const {
        std::vector<OrderId> out;
        for (const Entry& e : orders_) out.push_back(e.order.id);
        return out;
    }

private:
    struct Entry {
        Order order;
        std::uint64_t seq;
    };

    static bool better(const Entry& a, const Entry& b) {
        if (a.order.price != b.order.price) {
            return a.order.side == Side::Sell ? a.order.price < b.order.price
                                              : a.order.price > b.order.price;
        }
        return a.seq < b.seq;
    }

    std::vector<Entry> orders_;
    std::uint64_t seq_ = 0;
};

}  // namespace

TEST(Randomized, MatchesReferenceImplementation) {
    for (std::uint32_t seed = 1; seed <= 20; ++seed) {
        std::mt19937_64 rng(seed);
        OrderBook book;
        ReferenceBook ref;
        OrderId next_id = 1;

        for (int step = 0; step < 2000; ++step) {
            const int action = static_cast<int>(rng() % 10);
            const Side side = (rng() % 2) ? Side::Buy : Side::Sell;
            const Quantity qty = 1 + rng() % 20;

            if (action < 6) {  // limit order, prices clustered so they cross often
                const Price px = 95 + static_cast<Price>(rng() % 11);
                const OrderId id = next_id++;
                auto got = book.add_limit(id, side, px, qty);
                auto want = ref.add(id, side, px, qty);
                ASSERT_EQ(got.trades, want) << "seed=" << seed << " step=" << step;
            } else if (action < 7) {  // market order
                const OrderId id = next_id++;
                auto got = book.add_market(id, side, qty);
                auto want = ref.add(id, side, std::nullopt, qty);
                ASSERT_EQ(got.trades, want) << "seed=" << seed << " step=" << step;
            } else {  // cancel a random id (may or may not exist)
                const OrderId id = 1 + rng() % next_id;
                ASSERT_EQ(book.cancel(id), ref.cancel(id)) << "seed=" << seed << " step=" << step;
            }

            ASSERT_EQ(book.depth(Side::Buy), ref.depth(Side::Buy)) << "seed=" << seed;
            ASSERT_EQ(book.depth(Side::Sell), ref.depth(Side::Sell)) << "seed=" << seed;
            ASSERT_EQ(book.order_count(), ref.ids().size());

            // The book must never be crossed after matching.
            if (book.best_bid() && book.best_ask()) {
                ASSERT_LT(*book.best_bid(), *book.best_ask());
            }
        }
    }
}
