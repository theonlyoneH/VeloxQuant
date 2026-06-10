// tests/test_order_book.cpp  ── OrderBook unit tests
#include "exchange/order_book.hpp"
#include <gtest/gtest.h>

using namespace exch;
using namespace md;

namespace {

// Helper: create a basic resting limit order
Order make_limit(OrderId id, SymbolId sym, Side side,
                 Price price, Quantity qty) {
    Order o{};
    o.id          = id;
    o.symbol_id   = sym;
    o.side        = side;
    o.type        = OrderType::Limit;
    o.limit_price = price;
    o.qty         = qty;
    o.status      = OrderStatus::New;
    return o;
}

} // anon

// ── Empty book ────────────────────────────────────────────────────────────────
TEST(OrderBook, EmptyBook) {
    OrderBook book(1);
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_FALSE(book.mid_price().has_value());
    EXPECT_EQ(book.order_count(), 0u);
}

// ── Single bid ────────────────────────────────────────────────────────────────
TEST(OrderBook, SingleBid) {
    OrderBook book(1);
    auto o = make_limit(1, 1, Side::Buy, to_price(100.0), 500);
    book.add(o);

    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(*book.best_bid(), to_price(100.0));
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_EQ(book.order_count(), 1u);
}

// ── Best bid/ask ordering ─────────────────────────────────────────────────────
TEST(OrderBook, BestBidAsk) {
    OrderBook book(1);
    auto b1 = make_limit(1, 1, Side::Buy,  to_price(99.0),  100);
    auto b2 = make_limit(2, 1, Side::Buy,  to_price(100.0), 200);  // best bid
    auto a1 = make_limit(3, 1, Side::Sell, to_price(101.0), 300);  // best ask
    auto a2 = make_limit(4, 1, Side::Sell, to_price(102.0), 400);

    book.add(b1);
    book.add(b2);
    book.add(a1);
    book.add(a2);

    EXPECT_EQ(*book.best_bid(), to_price(100.0));
    EXPECT_EQ(*book.best_ask(), to_price(101.0));

    auto mid = book.mid_price();
    ASSERT_TRUE(mid.has_value());
    EXPECT_NEAR(from_price(*mid), 100.5, 0.001);

    auto sp = book.spread();
    ASSERT_TRUE(sp.has_value());
    EXPECT_NEAR(from_price(*sp), 1.0, 0.001);
}

// ── FIFO queue position ───────────────────────────────────────────────────────
TEST(OrderBook, FIFOQueuePosition) {
    OrderBook book(1);

    // Three bids at same price – queue order matters
    auto o1 = make_limit(1, 1, Side::Buy, to_price(100.0), 100);
    auto o2 = make_limit(2, 1, Side::Buy, to_price(100.0), 200);
    auto o3 = make_limit(3, 1, Side::Buy, to_price(100.0), 300);

    book.add(o1);
    book.add(o2);
    book.add(o3);

    // o1 is first → 0 shares ahead
    EXPECT_EQ(book.queue_position(1), 0u);
    // o2 has o1 (100 shares) ahead
    EXPECT_EQ(book.queue_position(2), 100u);
    // o3 has o1+o2 (300 shares) ahead
    EXPECT_EQ(book.queue_position(3), 300u);
}

// ── cancel ────────────────────────────────────────────────────────────────────
TEST(OrderBook, Cancel) {
    OrderBook book(1);
    auto o = make_limit(1, 1, Side::Buy, to_price(100.0), 500);
    book.add(o);
    EXPECT_EQ(book.order_count(), 1u);

    bool ok = book.cancel(1);
    EXPECT_TRUE(ok);
    EXPECT_EQ(book.order_count(), 0u);
    EXPECT_FALSE(book.best_bid().has_value());

    // Double-cancel → false
    EXPECT_FALSE(book.cancel(1));
}

// ── cancel restores FIFO after head removal ───────────────────────────────────
TEST(OrderBook, CancelMiddle) {
    OrderBook book(1);
    auto o1 = make_limit(1, 1, Side::Buy, to_price(100.0), 100);
    auto o2 = make_limit(2, 1, Side::Buy, to_price(100.0), 200);
    auto o3 = make_limit(3, 1, Side::Buy, to_price(100.0), 300);
    book.add(o1); book.add(o2); book.add(o3);

    book.cancel(2);

    // o3's shares ahead should now be just o1 (100)
    EXPECT_EQ(book.queue_position(3), 100u);
    EXPECT_EQ(book.order_count(), 2u);
}

// ── depth() ───────────────────────────────────────────────────────────────────
TEST(OrderBook, Depth) {
    OrderBook book(1);
    for (int i = 1; i <= 5; ++i) {
        auto o = make_limit(i, 1, Side::Sell,
                            to_price(100.0 + i), static_cast<Quantity>(i * 100));
        book.add(o);
    }

    auto depth = book.ask_depth(3);
    ASSERT_EQ(depth.size(), 3u);
    EXPECT_NEAR(from_price(depth[0].price), 101.0, 0.001);
    EXPECT_NEAR(from_price(depth[1].price), 102.0, 0.001);
    EXPECT_NEAR(from_price(depth[2].price), 103.0, 0.001);
}

// ── reduce_qty partial fill ───────────────────────────────────────────────────
TEST(OrderBook, ReduceQtyPartial) {
    OrderBook book(1);
    auto o = make_limit(1, 1, Side::Sell, to_price(101.0), 1000);
    book.add(o);

    book.reduce_qty(1, 400);
    const Order* ptr = book.find(1);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(ptr->filled_qty, 400u);
    EXPECT_EQ(ptr->leaves_qty(), 600u);
    EXPECT_EQ(ptr->status, OrderStatus::PartiallyFilled);
    // Order still in book
    ASSERT_TRUE(book.best_ask().has_value());
}

// ── reduce_qty full fill removes from book ────────────────────────────────────
TEST(OrderBook, ReduceQtyFullFill) {
    OrderBook book(1);
    auto o = make_limit(1, 1, Side::Sell, to_price(101.0), 500);
    book.add(o);

    book.reduce_qty(1, 500);
    EXPECT_EQ(book.ask_levels(), 0u);
    EXPECT_FALSE(book.best_ask().has_value());
}

// ── Multiple price levels, best tracks correctly ──────────────────────────────
TEST(OrderBook, MultipleLevelBestTracking) {
    OrderBook book(1);
    auto a1 = make_limit(1, 1, Side::Sell, to_price(102.0), 100);
    auto a2 = make_limit(2, 1, Side::Sell, to_price(101.0), 200);  // best
    book.add(a1); book.add(a2);
    EXPECT_EQ(*book.best_ask(), to_price(101.0));

    // Cancel best ask → a1 becomes best
    book.cancel(2);
    EXPECT_EQ(*book.best_ask(), to_price(102.0));
}
