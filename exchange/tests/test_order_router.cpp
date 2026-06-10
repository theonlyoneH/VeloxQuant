// tests/test_order_router.cpp  ── OrderRouter end-to-end integration tests
#include "exchange/order_router.hpp"
#include <gtest/gtest.h>
#include <vector>

using namespace exch;
using namespace md;

// ── Fixture ───────────────────────────────────────────────────────────────────
class OrderRouterTest : public ::testing::Test {
protected:
    static constexpr SymbolId kSym = 1;
    static constexpr Timestamp kT0 = 0;

    // No slippage, no commission – cleaner assertions
    OrderRouter router{ExecutionSimConfig{
        .slippage_model   = SlippageModel::None,
        .commission_model = CommissionModel::None,
    }};

    std::vector<Fill>            fills;
    std::vector<ExecutionReport> reports;

    void SetUp() override {
        router.register_symbol(kSym);
        router.on_fill([this](const Fill& f){ fills.push_back(f); });
        router.on_report([this](const ExecutionReport& r){ reports.push_back(r); });
    }

    // Helper: seed a resting passive limit
    OrderId seed_limit(Side side, Price price, Quantity qty,
                       Timestamp t = kT0) {
        return router.submit_limit(kSym, side, price, qty, TimeInForce::GTC, t);
    }
};

// ── Register unknown symbol → reject ─────────────────────────────────────────
TEST_F(OrderRouterTest, UnknownSymbolReject) {
    Order o{};
    o.symbol_id   = 999; // not registered
    o.side        = Side::Buy;
    o.type        = OrderType::Market;
    o.qty         = 100;

    const OrderId id = router.submit(o, kT0);
    EXPECT_EQ(id, kInvalidOrderId);
    EXPECT_EQ(o.status, OrderStatus::Rejected);
    EXPECT_EQ(o.reject_reason, RejectReason::UnknownSymbol);
}

// ── Zero quantity → reject ────────────────────────────────────────────────────
TEST_F(OrderRouterTest, ZeroQtyReject) {
    Order o{};
    o.symbol_id   = kSym;
    o.side        = Side::Buy;
    o.type        = OrderType::Market;
    o.qty         = 0;  // invalid

    const OrderId id = router.submit(o, kT0);
    EXPECT_EQ(id, kInvalidOrderId);
    EXPECT_EQ(o.status, OrderStatus::Rejected);
}

// ── OrderId increments ────────────────────────────────────────────────────────
TEST_F(OrderRouterTest, OrderIdMonotone) {
    seed_limit(Side::Buy, to_price(99.0), 100);
    seed_limit(Side::Buy, to_price(99.0), 200);
    // IDs should be 1, 2
    EXPECT_EQ(router.last_order_id(), 2u);
}

// ── Limit buy rests, then filled by sell market ───────────────────────────────
TEST_F(OrderRouterTest, LimitRestFilledByMarket) {
    // Seed a resting bid
    seed_limit(Side::Buy, to_price(100.0), 500);
    ASSERT_TRUE(router.book(kSym)->best_bid().has_value());

    // Aggressor: market sell
    const OrderId id =
        router.submit_market(kSym, Side::Sell, 500, kT0 + 1);
    EXPECT_NE(id, kInvalidOrderId);

    // Both sides should be filled
    ASSERT_GE(fills.size(), 2u);
    bool found_resting = false;
    for (const auto& f : fills) {
        if (!f.is_aggressive) { found_resting = true; break; }
    }
    EXPECT_TRUE(found_resting);
    EXPECT_FALSE(router.book(kSym)->best_bid().has_value());
}

// ── IOC submit → fill + cancel residual ──────────────────────────────────────
TEST_F(OrderRouterTest, IOCFillAndCancelResidual) {
    seed_limit(Side::Sell, to_price(100.0), 200);

    Order o{};
    o.symbol_id   = kSym;
    o.side        = Side::Buy;
    o.type        = OrderType::IOC;
    o.limit_price = to_price(100.0);
    o.qty         = 500;  // more than available

    router.submit(o, kT0 + 1);
    EXPECT_EQ(o.filled_qty, 200u);
    EXPECT_EQ(o.status, OrderStatus::Cancelled);
}

// ── FOK reject ───────────────────────────────────────────────────────────────
TEST_F(OrderRouterTest, FOKRejectInsufficientLiquidity) {
    seed_limit(Side::Sell, to_price(100.0), 100);

    Order o{};
    o.symbol_id   = kSym;
    o.side        = Side::Buy;
    o.type        = OrderType::FOK;
    o.limit_price = to_price(100.0);
    o.qty         = 300;

    const OrderId id = router.submit(o, kT0 + 1);
    EXPECT_NE(id, kInvalidOrderId);
    EXPECT_EQ(o.status, OrderStatus::Rejected);
    EXPECT_EQ(fills.size(), 0u);
}

// ── FOK fill entirely ────────────────────────────────────────────────────────
TEST_F(OrderRouterTest, FOKFillEntirely) {
    seed_limit(Side::Sell, to_price(100.0), 500);

    Order o{};
    o.symbol_id   = kSym;
    o.side        = Side::Buy;
    o.type        = OrderType::FOK;
    o.limit_price = to_price(100.0);
    o.qty         = 300;

    router.submit(o, kT0 + 1);
    EXPECT_EQ(o.status, OrderStatus::Filled);
    EXPECT_EQ(o.filled_qty, 300u);
}

// ── Cancel removes from book ──────────────────────────────────────────────────
TEST_F(OrderRouterTest, CancelOrder) {
    const OrderId id = seed_limit(Side::Buy, to_price(99.0), 400);
    ASSERT_TRUE(router.book(kSym)->best_bid().has_value());

    const bool ok = router.cancel(id, kT0 + 1);
    EXPECT_TRUE(ok);
    EXPECT_FALSE(router.book(kSym)->best_bid().has_value());
}

// ── Cancel unknown id → false ─────────────────────────────────────────────────
TEST_F(OrderRouterTest, CancelUnknownId) {
    EXPECT_FALSE(router.cancel(9999, kT0));
}

// ── Stop order activates on tick ──────────────────────────────────────────────
TEST_F(OrderRouterTest, StopTriggerOnTick) {
    // Seed ask for the stop to fill against
    seed_limit(Side::Sell, to_price(105.0), 1000);

    // Submit a buy stop: triggers when last_price >= 103
    const OrderId stop_id =
        router.submit_stop(kSym, Side::Buy, to_price(103.0), 200, kT0);
    EXPECT_NE(stop_id, kInvalidOrderId);
    EXPECT_EQ(fills.size(), 0u); // not yet triggered

    // Feed a tick that crosses the stop
    md::Tick tick{};
    tick.symbol_id  = kSym;
    tick.last_price = to_price(104.0);
    router.on_tick(tick, kT0 + 1);

    // Should have generated fills
    EXPECT_GT(fills.size(), 0u);
}

// ── Convenience submit_market ─────────────────────────────────────────────────
TEST_F(OrderRouterTest, ConvenienceSubmitMarket) {
    seed_limit(Side::Sell, to_price(100.0), 300);
    const OrderId id = router.submit_market(kSym, Side::Buy, 100, kT0);
    EXPECT_NE(id, kInvalidOrderId);
    EXPECT_GT(fills.size(), 0u);
}

// ── Counters increment correctly ──────────────────────────────────────────────
TEST_F(OrderRouterTest, CountersIncrement) {
    seed_limit(Side::Sell, to_price(100.0), 1000);

    router.submit_market(kSym, Side::Buy, 100, kT0);
    router.submit_market(kSym, Side::Buy, 100, kT0);

    EXPECT_GE(router.total_orders_submitted(), 3u); // 1 seeded + 2 markets
    EXPECT_GE(router.total_fills(), 2u);
}

// ── Multi-level sweep via router ──────────────────────────────────────────────
TEST_F(OrderRouterTest, MultiLevelSweep) {
    seed_limit(Side::Sell, to_price(100.0), 200);
    seed_limit(Side::Sell, to_price(101.0), 200);
    seed_limit(Side::Sell, to_price(102.0), 200);

    Order o{};
    o.symbol_id   = kSym;
    o.side        = Side::Buy;
    o.type        = OrderType::Market;
    o.qty         = 600;
    router.submit(o, kT0);

    EXPECT_EQ(o.filled_qty, 600u);
    EXPECT_EQ(o.status, OrderStatus::Filled);
    EXPECT_EQ(router.book(kSym)->ask_levels(), 0u);
}

// ── Multiple registered symbols independent ───────────────────────────────────
TEST_F(OrderRouterTest, MultipleSymbols) {
    constexpr SymbolId kSym2 = 2;
    router.register_symbol(kSym2);
    EXPECT_TRUE(router.has_symbol(kSym2));

    seed_limit(Side::Sell, to_price(50.0), 100);
    router.submit_limit(kSym2, Side::Sell, to_price(200.0), 100, TimeInForce::GTC, kT0);

    // Books are independent
    EXPECT_EQ(*router.book(kSym)->best_ask(),  to_price(50.0));
    EXPECT_EQ(*router.book(kSym2)->best_ask(), to_price(200.0));
}
