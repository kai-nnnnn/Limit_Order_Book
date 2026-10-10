#include "orderbook/order_book.hpp"

#include <gtest/gtest.h>

using namespace ob;

namespace {

// Small helper so expectations read like the scenario.
Trade T(OrderId maker, OrderId taker, Side taker_side, Price px, Quantity qty) {
    return Trade{maker, taker, taker_side, px, qty};
}

}  // namespace

// ---------- resting orders, no matching ----------

TEST(OrderBook, EmptyBookHasNoBestPrices) {
    OrderBook book;
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_TRUE(book.empty());
}

TEST(OrderBook, NonCrossingOrdersRest) {
    OrderBook book;
    auto r1 = book.add_limit(1, Side::Buy, 99, 10);
    auto r2 = book.add_limit(2, Side::Sell, 101, 5);

    EXPECT_TRUE(r1.trades.empty());
    EXPECT_TRUE(r2.trades.empty());
    EXPECT_EQ(r1.resting, 10u);
    EXPECT_EQ(book.best_bid(), 99);
    EXPECT_EQ(book.best_ask(), 101);
    EXPECT_EQ(book.order_count(), 2u);
}

TEST(OrderBook, BestBidIsHighestAndBestAskIsLowest) {
    OrderBook book;
    book.add_limit(1, Side::Buy, 98, 1);
    book.add_limit(2, Side::Buy, 100, 1);
    book.add_limit(3, Side::Buy, 99, 1);
    book.add_limit(4, Side::Sell, 103, 1);
    book.add_limit(5, Side::Sell, 101, 1);
    book.add_limit(6, Side::Sell, 102, 1);

    EXPECT_EQ(book.best_bid(), 100);
    EXPECT_EQ(book.best_ask(), 101);
}

TEST(OrderBook, SamePriceOrdersAggregateIntoOneLevel) {
    OrderBook book;
    book.add_limit(1, Side::Buy, 100, 3);
    book.add_limit(2, Side::Buy, 100, 4);

    EXPECT_EQ(book.volume_at(Side::Buy, 100), 7u);
    EXPECT_EQ(book.depth(Side::Buy), (std::vector<LevelSummary>{{100, 7, 2}}));
}

// ---------- matching ----------

TEST(OrderBook, FullFillRemovesMakerAndLevel) {
    OrderBook book;
    book.add_limit(1, Side::Sell, 100, 5);
    auto r = book.add_limit(2, Side::Buy, 100, 5);

    EXPECT_EQ(r.trades, (std::vector<Trade>{T(1, 2, Side::Buy, 100, 5)}));
    EXPECT_EQ(r.filled, 5u);
    EXPECT_EQ(r.resting, 0u);
    EXPECT_TRUE(book.empty());
    EXPECT_FALSE(book.best_ask().has_value());
}

TEST(OrderBook, TakerRemainderRestsOnBook) {
    OrderBook book;
    book.add_limit(1, Side::Sell, 100, 3);
    auto r = book.add_limit(2, Side::Buy, 100, 10);

    EXPECT_EQ(r.filled, 3u);
    EXPECT_EQ(r.resting, 7u);
    EXPECT_EQ(book.best_bid(), 100);
    EXPECT_EQ(book.volume_at(Side::Buy, 100), 7u);
    EXPECT_FALSE(book.best_ask().has_value());
}

TEST(OrderBook, MakerPartiallyFilledKeepsRemainder) {
    OrderBook book;
    book.add_limit(1, Side::Sell, 100, 10);
    book.add_limit(2, Side::Buy, 100, 4);

    EXPECT_TRUE(book.contains(1));
    EXPECT_EQ(book.volume_at(Side::Sell, 100), 6u);
}

TEST(OrderBook, TradesAtMakerPrice) {
    OrderBook book;
    book.add_limit(1, Side::Sell, 100, 5);
    auto r = book.add_limit(2, Side::Buy, 105, 5);  // willing to pay 105, gets 100

    ASSERT_EQ(r.trades.size(), 1u);
    EXPECT_EQ(r.trades[0].price, 100);
}

TEST(OrderBook, PricePriority) {
    OrderBook book;
    book.add_limit(1, Side::Sell, 102, 5);
    book.add_limit(2, Side::Sell, 101, 5);  // better price, arrived later
    auto r = book.add_limit(3, Side::Buy, 102, 5);

    EXPECT_EQ(r.trades, (std::vector<Trade>{T(2, 3, Side::Buy, 101, 5)}));
}

TEST(OrderBook, TimePriorityWithinLevel) {
    OrderBook book;
    book.add_limit(1, Side::Buy, 100, 5);
    book.add_limit(2, Side::Buy, 100, 5);
    auto r = book.add_limit(3, Side::Sell, 100, 7);

    EXPECT_EQ(r.trades, (std::vector<Trade>{T(1, 3, Side::Sell, 100, 5),
                                            T(2, 3, Side::Sell, 100, 2)}));
    EXPECT_EQ(book.volume_at(Side::Buy, 100), 3u);
}

TEST(OrderBook, WalksMultipleLevelsAndStopsAtLimit) {
    OrderBook book;
    book.add_limit(1, Side::Sell, 101, 2);
    book.add_limit(2, Side::Sell, 102, 2);
    book.add_limit(3, Side::Sell, 103, 2);
    auto r = book.add_limit(4, Side::Buy, 102, 10);

    EXPECT_EQ(r.trades, (std::vector<Trade>{T(1, 4, Side::Buy, 101, 2),
                                            T(2, 4, Side::Buy, 102, 2)}));
    EXPECT_EQ(r.resting, 6u);
    EXPECT_EQ(book.best_bid(), 102);
    EXPECT_EQ(book.best_ask(), 103);  // 103 was not touched
}

TEST(OrderBook, Lesson1Scenario) {
    OrderBook book;
    book.add_limit(1, Side::Buy, 99, 10);
    book.add_limit(2, Side::Sell, 101, 3);
    book.add_limit(3, Side::Sell, 102, 5);
    book.add_limit(4, Side::Sell, 101, 4);

    auto r = book.add_limit(5, Side::Buy, 101, 5);
    EXPECT_EQ(r.trades, (std::vector<Trade>{T(2, 5, Side::Buy, 101, 3),
                                            T(4, 5, Side::Buy, 101, 2)}));
    EXPECT_EQ(book.volume_at(Side::Sell, 101), 2u);
    EXPECT_FALSE(book.contains(5));
}

// ---------- market orders ----------

TEST(OrderBook, MarketOrderSweepsAnyPrice) {
    OrderBook book;
    book.add_limit(1, Side::Buy, 100, 2);
    book.add_limit(2, Side::Buy, 90, 2);
    auto r = book.add_market(3, Side::Sell, 3);

    EXPECT_EQ(r.trades, (std::vector<Trade>{T(1, 3, Side::Sell, 100, 2),
                                            T(2, 3, Side::Sell, 90, 1)}));
    EXPECT_EQ(r.filled, 3u);
}

TEST(OrderBook, MarketOrderRemainderIsDiscarded) {
    OrderBook book;
    book.add_limit(1, Side::Sell, 100, 2);
    auto r = book.add_market(2, Side::Buy, 10);

    EXPECT_EQ(r.filled, 2u);
    EXPECT_EQ(r.resting, 0u);
    EXPECT_TRUE(book.empty());
    EXPECT_FALSE(book.contains(2));
}

TEST(OrderBook, MarketOrderOnEmptyBookDoesNothing) {
    OrderBook book;
    auto r = book.add_market(1, Side::Buy, 10);
    EXPECT_TRUE(r.accepted());
    EXPECT_EQ(r.filled, 0u);
    EXPECT_TRUE(book.empty());
}

// ---------- cancel ----------

TEST(OrderBook, CancelRemovesOrderAndEmptyLevel) {
    OrderBook book;
    book.add_limit(1, Side::Buy, 100, 5);
    book.add_limit(2, Side::Buy, 99, 5);

    EXPECT_TRUE(book.cancel(1));
    EXPECT_FALSE(book.contains(1));
    EXPECT_EQ(book.best_bid(), 99);  // level 100 must be gone
}

TEST(OrderBook, CancelMiddleOfQueueKeepsOthersInOrder) {
    OrderBook book;
    book.add_limit(1, Side::Sell, 100, 1);
    book.add_limit(2, Side::Sell, 100, 1);
    book.add_limit(3, Side::Sell, 100, 1);
    book.cancel(2);

    auto r = book.add_limit(4, Side::Buy, 100, 2);
    EXPECT_EQ(r.trades, (std::vector<Trade>{T(1, 4, Side::Buy, 100, 1),
                                            T(3, 4, Side::Buy, 100, 1)}));
}

TEST(OrderBook, CancelPartiallyFilledOrderUpdatesVolume) {
    OrderBook book;
    book.add_limit(1, Side::Sell, 100, 10);
    book.add_limit(2, Side::Sell, 100, 5);
    book.add_limit(3, Side::Buy, 100, 4);  // #1 now has 6 left

    EXPECT_TRUE(book.cancel(1));
    EXPECT_EQ(book.volume_at(Side::Sell, 100), 5u);
}

TEST(OrderBook, CancelUnknownOrFilledOrderFails) {
    OrderBook book;
    EXPECT_FALSE(book.cancel(42));

    book.add_limit(1, Side::Sell, 100, 5);
    book.add_limit(2, Side::Buy, 100, 5);  // fully fills #1
    EXPECT_FALSE(book.cancel(1));

    book.add_limit(3, Side::Buy, 90, 5);
    EXPECT_TRUE(book.cancel(3));
    EXPECT_FALSE(book.cancel(3));  // double cancel
}

// ---------- validation ----------

TEST(OrderBook, RejectsDuplicateIdOfRestingOrder) {
    OrderBook book;
    book.add_limit(1, Side::Buy, 100, 5);
    auto r = book.add_limit(1, Side::Sell, 200, 5);
    EXPECT_EQ(r.status, Status::RejectedDuplicateId);
    EXPECT_EQ(book.order_count(), 1u);
}

TEST(OrderBook, RejectsZeroQuantityAndNonPositivePrice) {
    OrderBook book;
    EXPECT_EQ(book.add_limit(1, Side::Buy, 100, 0).status, Status::RejectedZeroQuantity);
    EXPECT_EQ(book.add_market(2, Side::Buy, 0).status, Status::RejectedZeroQuantity);
    EXPECT_EQ(book.add_limit(3, Side::Buy, 0, 5).status, Status::RejectedInvalidPrice);
    EXPECT_EQ(book.add_limit(4, Side::Buy, -1, 5).status, Status::RejectedInvalidPrice);
    EXPECT_TRUE(book.empty());
}
