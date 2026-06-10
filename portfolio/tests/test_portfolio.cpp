// tests/test_portfolio.cpp  ── Portfolio and Position unit tests
#include "portfolio/portfolio.hpp"
#include <gtest/gtest.h>
#include <cmath>

using namespace port;
using namespace md;
using namespace exch;

namespace {

// Build a minimal Fill
Fill make_fill(SymbolId sym, Side side, double price, uint64_t qty,
               double commission = 0.0) {
    Fill f{};
    f.symbol_id   = sym;
    f.side        = side;
    f.fill_price  = to_price(price);
    f.fill_qty    = qty;
    f.commission  = to_price(commission);
    f.timestamp   = 0;
    return f;
}

PriceMap prices(SymbolId sym, double p) {
    return {{sym, p}};
}

} // anon

// ─────────────────────────────────────────────────────────────────────────────
// Position tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(Position, StartsFlat) {
    Position pos(1);
    EXPECT_TRUE(pos.is_flat());
    EXPECT_EQ(pos.qty(), 0);
    EXPECT_NEAR(pos.avg_cost(), 0.0, 1e-9);
    EXPECT_NEAR(pos.unrealized_pnl(100.0), 0.0, 1e-9);
}

TEST(Position, BuyOpensLongPosition) {
    Position pos(1);
    pos.on_fill(make_fill(1, Side::Buy, 100.0, 500));
    EXPECT_EQ(pos.qty(), 500);
    EXPECT_NEAR(pos.avg_cost(), 100.0, 1e-6);
    EXPECT_TRUE(pos.is_long());
}

TEST(Position, SellOpensShortPosition) {
    Position pos(1);
    pos.on_fill(make_fill(1, Side::Sell, 50.0, 200));
    EXPECT_EQ(pos.qty(), -200);
    EXPECT_NEAR(pos.avg_cost(), 50.0, 1e-6);
    EXPECT_TRUE(pos.is_short());
}

TEST(Position, UnrealizedPnLLong) {
    Position pos(1);
    pos.on_fill(make_fill(1, Side::Buy, 100.0, 100));
    // Bought at 100, now at 110 → unrealized = (110-100) × 100 = 1000
    EXPECT_NEAR(pos.unrealized_pnl(110.0), 1000.0, 1e-4);
    // At 90 → unrealized = -1000
    EXPECT_NEAR(pos.unrealized_pnl(90.0), -1000.0, 1e-4);
}

TEST(Position, UnrealizedPnLShort) {
    Position pos(1);
    pos.on_fill(make_fill(1, Side::Sell, 100.0, 100));
    // Sold at 100, now at 90 → profit = (100-90) × 100 = 1000
    EXPECT_NEAR(pos.unrealized_pnl(90.0),  1000.0, 1e-4);
    EXPECT_NEAR(pos.unrealized_pnl(110.0), -1000.0, 1e-4);
}

TEST(Position, PartialClose_RealizesPnL) {
    Position pos(1);
    pos.on_fill(make_fill(1, Side::Buy, 100.0, 1000));

    // Close half at 110
    pos.on_fill(make_fill(1, Side::Sell, 110.0, 500));

    EXPECT_EQ(pos.qty(), 500);
    EXPECT_NEAR(pos.avg_cost(), 100.0, 1e-6);  // unchanged for remaining
    // Realized: (110-100) × 500 = 5000
    EXPECT_NEAR(pos.realized_pnl(), 5000.0, 1e-3);
}

TEST(Position, FullClose_FlatAfter) {
    Position pos(1);
    pos.on_fill(make_fill(1, Side::Buy, 100.0, 200));
    pos.on_fill(make_fill(1, Side::Sell, 120.0, 200));

    EXPECT_TRUE(pos.is_flat());
    EXPECT_NEAR(pos.realized_pnl(), 4000.0, 1e-3); // (120-100) × 200
    EXPECT_NEAR(pos.unrealized_pnl(150.0), 0.0, 1e-9);
}

TEST(Position, AverageCostMultipleFills) {
    Position pos(1);
    // Buy 100 @ 100 and 100 @ 120 → avg = 110
    pos.on_fill(make_fill(1, Side::Buy, 100.0, 100));
    pos.on_fill(make_fill(1, Side::Buy, 120.0, 100));
    EXPECT_EQ(pos.qty(), 200);
    EXPECT_NEAR(pos.avg_cost(), 110.0, 1e-6);
}

TEST(Position, Reverse_LongToShort) {
    Position pos(1);
    pos.on_fill(make_fill(1, Side::Buy,  100.0, 100));  // +100
    pos.on_fill(make_fill(1, Side::Sell, 110.0, 150));  // -150 → net -50

    // Should realize P&L on the 100 closed, then open short 50
    EXPECT_EQ(pos.qty(), -50);
    EXPECT_TRUE(pos.is_short());
    // Realized on closing 100 longs: (110-100) × 100 = 1000
    EXPECT_NEAR(pos.realized_pnl(), 1000.0, 1e-3);
    // Avg cost of short position = 110
    EXPECT_NEAR(pos.avg_cost(), 110.0, 1e-6);
}

TEST(Position, GrossExposure) {
    Position pos(1);
    pos.on_fill(make_fill(1, Side::Buy, 50.0, 200));
    EXPECT_NEAR(pos.gross_exposure(60.0), 200.0 * 60.0, 1e-4);
}

// ─────────────────────────────────────────────────────────────────────────────
// Portfolio tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(Portfolio, InitialState) {
    Portfolio p(500'000.0);
    EXPECT_NEAR(p.cash(), 500'000.0, 1.0);
    EXPECT_EQ(p.fill_count(), 0u);
    EXPECT_TRUE(p.positions().empty());
}

TEST(Portfolio, BuyDeductsCash) {
    Portfolio p(100'000.0);
    p.on_fill(make_fill(1, Side::Buy, 50.0, 100));
    // cash = 100000 - 50×100 = 95000
    EXPECT_NEAR(p.cash(), 95'000.0, 1e-3);
}

TEST(Portfolio, SellAddsCash) {
    Portfolio p(100'000.0);
    p.on_fill(make_fill(1, Side::Buy,  50.0, 100));
    p.on_fill(make_fill(1, Side::Sell, 60.0, 100));
    // cash = 100000 - 5000 + 6000 = 101000
    EXPECT_NEAR(p.cash(), 101'000.0, 1e-3);
}

TEST(Portfolio, CommissionDeducted) {
    Portfolio p(100'000.0);
    p.on_fill(make_fill(1, Side::Buy, 100.0, 100, /*commission=*/10.0));
    // cash = 100000 - 100×100 - 10 = 89990
    EXPECT_NEAR(p.cash(), 89'990.0, 1e-3);
}

TEST(Portfolio, NAVEquityCash) {
    Portfolio p(100'000.0);
    p.on_fill(make_fill(1, Side::Buy, 100.0, 100));
    // cash = 90000, position 100 shares @ 110
    auto pm = prices(1, 110.0);
    // NAV = 90000 + 100×110 = 101000
    EXPECT_NEAR(p.nav(pm), 101'000.0, 1e-3);
}

TEST(Portfolio, NAVNoPositions) {
    Portfolio p(200'000.0);
    PriceMap pm;
    EXPECT_NEAR(p.nav(pm), 200'000.0, 1e-3);
}

TEST(Portfolio, RealizedPnL) {
    Portfolio p(100'000.0);
    p.on_fill(make_fill(1, Side::Buy,  50.0, 200));
    p.on_fill(make_fill(1, Side::Sell, 70.0, 200));
    // realized = (70-50) × 200 = 4000
    EXPECT_NEAR(p.realized_pnl(), 4000.0, 1e-3);
}

TEST(Portfolio, UnrealizedPnL) {
    Portfolio p(100'000.0);
    p.on_fill(make_fill(1, Side::Buy, 100.0, 100));
    auto pm = prices(1, 120.0);
    EXPECT_NEAR(p.unrealized_pnl(pm), 2000.0, 1e-3); // (120-100)×100
}

TEST(Portfolio, GrossAndNetExposure) {
    Portfolio p(200'000.0);
    // Long 100 shares of sym 1 @ $50, short 50 shares of sym 2 @ $80
    p.on_fill(make_fill(1, Side::Buy,  50.0, 100));
    p.on_fill(make_fill(2, Side::Sell, 80.0,  50));

    PriceMap pm{{1, 50.0}, {2, 80.0}};
    // Gross = 100×50 + 50×80 = 9000
    EXPECT_NEAR(p.gross_exposure(pm), 9000.0, 1e-3);
    // Net = +100×50 - 50×80 = 5000 - 4000 = 1000
    EXPECT_NEAR(p.net_exposure(pm), 1000.0, 1e-3);
}

TEST(Portfolio, TotalReturn) {
    Portfolio p(100'000.0);
    p.on_fill(make_fill(1, Side::Buy, 100.0, 100));
    auto pm = prices(1, 110.0);
    // NAV = 90000 + 100×110 = 101000
    // total_return = (101000-100000)/100000 = 0.01
    EXPECT_NEAR(p.total_return(pm), 0.01, 1e-5);
}

TEST(Portfolio, EquitySnapshot) {
    Portfolio p(100'000.0);
    PriceMap pm{{1, 100.0}};
    p.snapshot_equity(1000, pm);
    p.snapshot_equity(2000, pm);

    const auto& curve = p.equity_curve();
    ASSERT_EQ(curve.size(), 2u);
    EXPECT_EQ(curve[0].timestamp, 1000);
    EXPECT_EQ(curve[1].timestamp, 2000);
}

TEST(Portfolio, FillCount) {
    Portfolio p(100'000.0);
    p.on_fill(make_fill(1, Side::Buy, 50.0, 100));
    p.on_fill(make_fill(1, Side::Sell, 55.0, 50));
    EXPECT_EQ(p.fill_count(), 2u);
}

TEST(Portfolio, PositionLookup) {
    Portfolio p(100'000.0);
    p.on_fill(make_fill(1, Side::Buy, 50.0, 200));
    const Position* pos = p.position(1);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->qty(), 200);

    EXPECT_EQ(p.position(99), nullptr);
}
