// tests/test_matching_engine.cpp  ── MatchingEngine unit tests
#include "exchange/execution_sim.hpp"
#include "exchange/matching_engine.hpp"
#include "exchange/order_book.hpp"

#include <gtest/gtest.h>
#include <vector>

using namespace exch;
using namespace md;

// ── Test fixture ──────────────────────────────────────────────────────────────
class MatchingEngineTest : public ::testing::Test {
protected:
    static constexpr SymbolId kSym = 42;
    static constexpr Timestamp kNow = 1'000'000'000LL;

    OrderBook          book{kSym};
    ExecutionSimulator exec_sim{ExecutionSimConfig{
        .slippage_model   = SlippageModel::None,
        .commission_model = CommissionModel::None,
    }};
    MatchingEngine     engine{book, exec_sim};

    std::vector<Fill>            fills;
    std::vector<ExecutionReport> reports;

    void SetUp() override {
        engine.on_fill([this](const Fill& f){ fills.push_back(f); });
        engine.on_report([this](const ExecutionReport& r){ reports.push_back(r); });
    }

    void seed_limit(OrderId id, Side side, Price price, Quantity qty) {
        Order o{};
        o.id          = id;
        o.symbol_id   = kSym;
        o.side        = side;
        o.type        = OrderType::Limit;
        o.limit_price = price;
        o.qty         = qty;
        o.status      = OrderStatus::New;
        book.add(o);
    }

    Order make_order(Side side, OrderType type, Price limit_price,
                     Quantity qty, Price stop_price = 0) {
        Order o{};
        o.symbol_id   = kSym;
        o.side        = side;
        o.type        = type;
        o.limit_price = limit_price;
        o.stop_price  = stop_price;
        o.qty         = qty;
        o.id          = 1000; // aggressor ids start at 1000 in these tests
        return o;
    }
};

// ── Market order hits single ask level ────────────────────────────────────────
TEST_F(MatchingEngineTest, MarketBuyFullFill) {
    seed_limit(1, Side::Sell, to_price(100.0), 500);

    auto o = make_order(Side::Buy, OrderType::Market, 0, 500);
    engine.process(o, kNow);

    ASSERT_EQ(fills.size(), 2u); // aggressor + resting
    EXPECT_EQ(fills[0].fill_qty, 500u);
    EXPECT_EQ(fills[0].fill_price, to_price(100.0));
    EXPECT_EQ(o.status, OrderStatus::Filled);
    EXPECT_EQ(book.ask_levels(), 0u);
}

// ── Market order partial fill (insufficient liquidity) ────────────────────────
TEST_F(MatchingEngineTest, MarketBuyPartialFill) {
    seed_limit(1, Side::Sell, to_price(100.0), 200); // only 200 available

    auto o = make_order(Side::Buy, OrderType::Market, 0, 500);
    engine.process(o, kNow);

    EXPECT_EQ(o.filled_qty, 200u);
    EXPECT_EQ(o.status, OrderStatus::Cancelled); // residual cancelled (market can't rest)
    EXPECT_EQ(fills.size(), 2u);
}

// ── Limit buy fills against asks within price barrier ─────────────────────────
TEST_F(MatchingEngineTest, LimitBuyMatchesWithinPrice) {
    seed_limit(1, Side::Sell, to_price(100.0), 300);
    seed_limit(2, Side::Sell, to_price(101.0), 300);
    seed_limit(3, Side::Sell, to_price(102.0), 300); // above limit

    auto o = make_order(Side::Buy, OrderType::Limit, to_price(101.0), 600);
    engine.process(o, kNow);

    // Should fill 300 @ 100 + 300 @ 101 = 600 total
    EXPECT_EQ(o.filled_qty, 600u);
    EXPECT_EQ(o.status, OrderStatus::Filled);
    // Level at 102 still intact
    EXPECT_EQ(book.ask_levels(), 1u);
}

// ── Limit buy with residual rests on book ─────────────────────────────────────
TEST_F(MatchingEngineTest, LimitBuyRestsOnBook) {
    seed_limit(1, Side::Sell, to_price(100.0), 200);

    auto o = make_order(Side::Buy, OrderType::Limit, to_price(100.0), 500);
    engine.process(o, kNow);

    EXPECT_EQ(o.filled_qty, 200u);
    EXPECT_EQ(o.status, OrderStatus::PartiallyFilled);
    // Residual 300 should be resting as a bid
    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(*book.best_bid(), to_price(100.0));
}

// ── FIFO within a price level ─────────────────────────────────────────────────
TEST_F(MatchingEngineTest, FIFOFilledInOrder) {
    seed_limit(10, Side::Sell, to_price(100.0), 100); // first in queue
    seed_limit(11, Side::Sell, to_price(100.0), 100); // second
    seed_limit(12, Side::Sell, to_price(100.0), 100); // third

    auto o = make_order(Side::Buy, OrderType::Market, 0, 150);
    engine.process(o, kNow);

    // Should fill order 10 fully (100) and order 11 partially (50)
    // Fills are emitted: agg-fill + rest-fill per execution
    // find the resting fill events
    std::vector<Fill> resting_fills;
    for (const auto& f : fills) {
        if (!f.is_aggressive) resting_fills.push_back(f);
    }

    ASSERT_GE(resting_fills.size(), 1u);
    // First resting fill is order 10
    EXPECT_EQ(resting_fills[0].order_id, 10u);
    EXPECT_EQ(resting_fills[0].fill_qty,  100u);
    if (resting_fills.size() >= 2) {
        EXPECT_EQ(resting_fills[1].order_id, 11u);
        EXPECT_EQ(resting_fills[1].fill_qty,  50u);
    }
}

// ── IOC: fill what can, cancel rest ──────────────────────────────────────────
TEST_F(MatchingEngineTest, IOCPartialFillThenCancel) {
    seed_limit(1, Side::Sell, to_price(100.0), 300);
    // Nothing at 101 → IOC residual should be cancelled

    auto o = make_order(Side::Buy, OrderType::IOC, to_price(100.0), 500);
    engine.process(o, kNow);

    EXPECT_EQ(o.filled_qty, 300u);
    EXPECT_EQ(o.status, OrderStatus::Cancelled); // residual cancelled
    EXPECT_EQ(book.bid_levels(), 0u);  // nothing resting
}

// ── IOC: fully fills when liquidity sufficient ────────────────────────────────
TEST_F(MatchingEngineTest, IOCFullFill) {
    seed_limit(1, Side::Sell, to_price(100.0), 600);

    auto o = make_order(Side::Buy, OrderType::IOC, to_price(100.0), 500);
    engine.process(o, kNow);

    EXPECT_EQ(o.filled_qty, 500u);
    EXPECT_EQ(o.status, OrderStatus::Filled);
}

// ── FOK: rejects if cannot fill entirely ─────────────────────────────────────
TEST_F(MatchingEngineTest, FOKRejectInsufficientLiquidity) {
    seed_limit(1, Side::Sell, to_price(100.0), 200);

    auto o = make_order(Side::Buy, OrderType::FOK, to_price(100.0), 500);
    engine.process(o, kNow);

    EXPECT_EQ(o.status, OrderStatus::Rejected);
    EXPECT_EQ(o.reject_reason, RejectReason::InsufficientLiquidity);
    EXPECT_EQ(o.filled_qty, 0u);
    // Resting order untouched
    EXPECT_EQ(*book.best_ask(), to_price(100.0));
    EXPECT_EQ(fills.size(), 0u); // no fills emitted
}

// ── FOK: fills entirely when sufficient liquidity ─────────────────────────────
TEST_F(MatchingEngineTest, FOKFullFill) {
    seed_limit(1, Side::Sell, to_price(100.0), 600);

    auto o = make_order(Side::Buy, OrderType::FOK, to_price(100.0), 500);
    engine.process(o, kNow);

    EXPECT_EQ(o.filled_qty, 500u);
    EXPECT_EQ(o.status, OrderStatus::Filled);
    EXPECT_GE(fills.size(), 1u);
}

// ── FOK: multi-level fill ─────────────────────────────────────────────────────
TEST_F(MatchingEngineTest, FOKMultiLevelFill) {
    seed_limit(1, Side::Sell, to_price(100.0), 300);
    seed_limit(2, Side::Sell, to_price(101.0), 300);

    // 600 available within limit 101 → FOK should fill entirely
    auto o = make_order(Side::Buy, OrderType::FOK, to_price(101.0), 600);
    engine.process(o, kNow);

    EXPECT_EQ(o.filled_qty, 600u);
    EXPECT_EQ(o.status, OrderStatus::Filled);
}

// ── Sell market sweeps bids ───────────────────────────────────────────────────
TEST_F(MatchingEngineTest, SellMarketSweepsBids) {
    seed_limit(1, Side::Buy, to_price(100.0), 400);
    seed_limit(2, Side::Buy, to_price(99.0),  400);

    auto o = make_order(Side::Sell, OrderType::Market, 0, 700);
    engine.process(o, kNow);

    EXPECT_EQ(o.filled_qty, 700u);
    EXPECT_EQ(o.status, OrderStatus::Filled);
}

// ── Multiple partial fills accumulate correctly ───────────────────────────────
TEST_F(MatchingEngineTest, MultiplePartialFillsAccumulate) {
    for (int i = 1; i <= 5; ++i) {
        seed_limit(i, Side::Sell,
                   to_price(100.0 + i * 0.01),
                   static_cast<Quantity>(100));
    }

    auto o = make_order(Side::Buy, OrderType::Market, 0, 500);
    engine.process(o, kNow);

    EXPECT_EQ(o.filled_qty, 500u);
    EXPECT_EQ(o.status, OrderStatus::Filled);
}

// ── Stop order: rests, then activates on tick ─────────────────────────────────
TEST_F(MatchingEngineTest, StopOrderActivation) {
    // Seed liquidity on ask side
    seed_limit(99, Side::Sell, to_price(105.0), 1000);

    // Submit a buy stop at 103 (triggers when price >= 103)
    Order stop_order{};
    stop_order.id         = 50;
    stop_order.symbol_id  = kSym;
    stop_order.side       = Side::Buy;
    stop_order.type       = OrderType::Stop;
    stop_order.stop_price = to_price(103.0);
    stop_order.qty        = 200;
    stop_order.status     = OrderStatus::New;
    engine.process(stop_order, kNow);

    // No fills yet
    EXPECT_EQ(fills.size(), 0u);
    EXPECT_EQ(stop_order.filled_qty, 0u);

    // Fire a tick that crosses the stop
    md::Tick tick{};
    tick.symbol_id  = kSym;
    tick.last_price = to_price(104.0); // > 103 → triggers
    engine.on_tick(tick, kNow + 1);

    // Stop should have been triggered and filled against the 105 ask
    EXPECT_GT(fills.size(), 0u);
}

// ── engine counters ───────────────────────────────────────────────────────────
TEST_F(MatchingEngineTest, Counters) {
    seed_limit(1, Side::Sell, to_price(100.0), 1000);
    auto o1 = make_order(Side::Buy, OrderType::Market, 0, 100);
    auto o2 = make_order(Side::Buy, OrderType::Market, 0, 100);
    engine.process(o1, kNow);
    engine.process(o2, kNow);
    EXPECT_EQ(engine.orders_processed(), 2u);
    EXPECT_GE(engine.fills_generated(), 2u);
}
