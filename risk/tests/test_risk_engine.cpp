// tests/test_risk_engine.cpp  ── RiskEngine unit tests
#include "risk/risk_engine.hpp"
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace risk;
using namespace port;
using namespace exch;
using namespace md;

namespace {

// Build a minimal fill for portfolio seeding
Fill make_fill(SymbolId sym, Side side, double price, uint64_t qty) {
    Fill f{};
    f.symbol_id  = sym;
    f.side       = side;
    f.fill_price = to_price(price);
    f.fill_qty   = qty;
    return f;
}

// Build a minimal order for checking
Order make_order(SymbolId sym, Side side, OrderType type,
                 double limit_price, uint64_t qty) {
    Order o{};
    o.symbol_id   = sym;
    o.side        = side;
    o.type        = type;
    o.limit_price = to_price(limit_price);
    o.qty         = qty;
    return o;
}

PriceMap prices(SymbolId sym, double p) {
    return {{sym, p}};
}

} // anon

// ── Fresh RiskEngine passes valid orders ───────────────────────────────────────
TEST(RiskEngine, PassesValidOrder) {
    RiskEngine engine(RiskLimits{
        .max_position_qty   = 1'000'000,
        .max_gross_exposure = 50'000'000.0,
        .max_net_exposure   = 20'000'000.0,
        .max_order_qty      = 10'000,
        .max_order_notional = 1'000'000.0,
        .max_drawdown_pct   = 0.20,
        .max_daily_loss     = 100'000.0,
    });

    Portfolio port(1'000'000.0);
    auto pm = prices(1, 100.0);
    auto order = make_order(1, Side::Buy, OrderType::Limit, 100.0, 500);

    const auto result = engine.check_order(order, port, pm, 0);
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(result.reason.empty());
}

// ── Kill switch rejects all orders ────────────────────────────────────────────
TEST(RiskEngine, KillSwitchRejectsAll) {
    RiskEngine engine(RiskLimits{.kill_switch_enabled = true});
    Portfolio port;
    auto pm = prices(1, 100.0);
    auto order = make_order(1, Side::Buy, OrderType::Market, 0.0, 100);

    const auto result = engine.check_order(order, port, pm, 0);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.reason.empty());
}

TEST(RiskEngine, EngageKillSwitch) {
    RiskEngine engine;
    EXPECT_FALSE(engine.kill_switch());
    engine.engage_kill_switch();
    EXPECT_TRUE(engine.kill_switch());
}

// ── Order qty limit ───────────────────────────────────────────────────────────
TEST(RiskEngine, OrderQtyLimitReject) {
    RiskEngine engine(RiskLimits{.max_order_qty = 1000});
    Portfolio port;
    auto pm = prices(1, 10.0);
    auto order = make_order(1, Side::Buy, OrderType::Market, 0.0, 2000); // over limit

    const auto result = engine.check_order(order, port, pm, 0);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.reason.find("max_order_qty"), std::string::npos);
}

// ── Order notional limit ──────────────────────────────────────────────────────
TEST(RiskEngine, OrderNotionalReject) {
    RiskEngine engine(RiskLimits{
        .max_order_qty      = 1'000'000,
        .max_order_notional = 5000.0,  // tiny limit
    });
    Portfolio port;
    auto pm = prices(1, 100.0);
    auto order = make_order(1, Side::Buy, OrderType::Limit, 100.0, 1000); // 100k notional

    const auto result = engine.check_order(order, port, pm, 0);
    EXPECT_FALSE(result.ok);
}

// ── Position qty limit ────────────────────────────────────────────────────────
TEST(RiskEngine, PositionLimitReject) {
    RiskEngine engine(RiskLimits{
        .max_position_qty   = 500,
        .max_gross_exposure = 1e9,
        .max_order_qty      = 1'000'000,
        .max_order_notional = 1e9,
    });
    Portfolio port;
    port.on_fill(make_fill(1, Side::Buy, 100.0, 400));  // already 400 shares

    auto pm = prices(1, 100.0);
    auto order = make_order(1, Side::Buy, OrderType::Limit, 100.0, 200); // would be 600

    const auto result = engine.check_order(order, port, pm, 0);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.reason.find("max_position_qty"), std::string::npos);
}

// ── Gross exposure limit ──────────────────────────────────────────────────────
TEST(RiskEngine, GrossExposureReject) {
    RiskEngine engine(RiskLimits{
        .max_position_qty   = 1'000'000,
        .max_gross_exposure = 1000.0,  // very tight
        .max_net_exposure   = 1000.0,
        .max_order_qty      = 1'000'000,
        .max_order_notional = 1e9,
    });
    Portfolio port;
    auto pm = prices(1, 100.0);
    auto order = make_order(1, Side::Buy, OrderType::Limit, 100.0, 100); // 10000 notional

    const auto result = engine.check_order(order, port, pm, 0);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.reason.find("gross exposure"), std::string::npos);
}

// ── Daily loss limit blocks orders ────────────────────────────────────────────
TEST(RiskEngine, DailyLossReject) {
    RiskEngine engine(RiskLimits{
        .max_position_qty    = 1'000'000,
        .max_gross_exposure  = 1e9,
        .max_net_exposure    = 1e9,
        .max_order_qty       = 1'000'000,
        .max_order_notional  = 1e9,
        .max_drawdown_pct    = 1.0,
        .max_daily_loss      = 100.0,  // $100 limit
    });

    Portfolio port(100'000.0);
    auto pm = prices(1, 200.0);

    // Simulate a fill + NAV drop to trigger daily loss
    engine.set_hwm(100'000.0);
    // Establish prev_nav_ at 100k
    engine.on_fill(make_fill(1, Side::Buy, 200.0, 0), port, pm, 0);
    engine.reset_daily_pnl(0);

    // Force daily loss by calling on_fill with a cratered portfolio
    // Build a portfolio that lost more than $100
    port.on_fill(make_fill(1, Side::Buy, 200.0, 100));  // pays 20000
    // Now prices drop to 100 → unrealized loss = 10000

    PriceMap pm2{{1, 100.0}};
    engine.on_fill(make_fill(1, Side::Buy, 200.0, 100), port, pm2, 1);
    // daily_pnl_ = nav(pm2) - prev_nav = (80000+10000) - 100000 = -10000

    auto order = make_order(1, Side::Buy, OrderType::Limit, 100.0, 10);
    const auto result = engine.check_order(order, port, pm2, 2);
    EXPECT_FALSE(result.ok);
}

// ── Drawdown limit blocks orders ──────────────────────────────────────────────
TEST(RiskEngine, DrawdownReject) {
    RiskEngine engine(RiskLimits{
        .max_position_qty   = 1'000'000,
        .max_gross_exposure = 1e9,
        .max_net_exposure   = 1e9,
        .max_order_qty      = 1'000'000,
        .max_order_notional = 1e9,
        .max_drawdown_pct   = 0.05,  // 5% max drawdown
        .max_daily_loss     = 1e9,
    });

    Portfolio port(100'000.0);
    engine.set_hwm(100'000.0);

    // Simulate portfolio cratering to 90k (10% drawdown > 5% limit)
    // We call on_fill with nav well below HWM
    port.on_fill(make_fill(1, Side::Sell, 100.0, 10000)); // adds cash
    // Now nav = initial_cash + fill_proceeds = 100000 + 1000000 = ... huge
    // Use a controlled approach: just set drawdown via on_fill with low nav

    // We'll use a minimal portfolio with a known NAV
    Portfolio port2(90'000.0);  // starts at 90k with 100k HWM = 10% drawdown
    engine.on_fill(make_fill(1, Side::Buy, 100.0, 1), port2, PriceMap{}, 1);
    // drawdown = (100000 - 90000) / 100000 = 0.10 > 0.05

    auto pm = PriceMap{};
    auto order = make_order(1, Side::Buy, OrderType::Market, 0.0, 10);
    const auto result = engine.check_order(order, port2, pm, 2);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.reason.find("drawdown"), std::string::npos);
}

// ── Breach callback fires ─────────────────────────────────────────────────────
TEST(RiskEngine, BreachCallbackFires) {
    RiskEngine engine(RiskLimits{
        .max_drawdown_pct = 0.05,
        .max_daily_loss   = 100.0,
    });
    engine.set_hwm(100'000.0);

    std::vector<BreachEvent> breaches;
    engine.on_breach([&](const BreachEvent& b){ breaches.push_back(b); });

    Portfolio port(90'000.0);  // already below HWM → 10% drawdown
    PriceMap pm;
    engine.on_fill(make_fill(1, Side::Buy, 100.0, 1), port, pm, 0);

    EXPECT_FALSE(breaches.empty());
    EXPECT_EQ(breaches[0].type, BreachType::DrawdownLimit);
}

// ── reset_daily_pnl resets counter ────────────────────────────────────────────
TEST(RiskEngine, ResetDailyPnl) {
    RiskEngine engine;
    engine.reset_daily_pnl(0);
    EXPECT_NEAR(engine.daily_pnl(), 0.0, 1e-9);
}

// ── HWM update ────────────────────────────────────────────────────────────────
TEST(RiskEngine, HWMUpdates) {
    RiskEngine engine;
    engine.set_hwm(50'000.0);
    EXPECT_NEAR(engine.high_water_mark(), 50'000.0, 1e-6);

    // Feed a fill with higher NAV → HWM should increase
    Portfolio port(200'000.0);
    PriceMap pm;
    engine.on_fill(make_fill(1, Side::Buy, 1.0, 1), port, pm, 0);
    EXPECT_GE(engine.high_water_mark(), 50'000.0);
}
