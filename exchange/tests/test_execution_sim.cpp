// tests/test_execution_sim.cpp  ── ExecutionSimulator unit tests
#include "exchange/execution_sim.hpp"
#include <gtest/gtest.h>
#include <cmath>

using namespace exch;
using namespace md;

// ── Commission: None ──────────────────────────────────────────────────────────
TEST(ExecutionSim, CommissionNone) {
    ExecutionSimulator sim({.commission_model = CommissionModel::None});
    EXPECT_EQ(sim.compute_commission(1000, to_price(100.0)), 0);
}

// ── Commission: PerShare ─────────────────────────────────────────────────────
TEST(ExecutionSim, CommissionPerShare) {
    ExecutionSimulator sim({
        .commission_model = CommissionModel::PerShare,
        .per_share_rate   = 0.005,   // $0.005 per share
    });
    const Price comm = sim.compute_commission(1000, to_price(50.0));
    // Expected: to_price(0.005) * 1000 = $5.00
    EXPECT_NEAR(from_price(comm), 5.0, 0.01);
}

// ── Commission: PerTrade ─────────────────────────────────────────────────────
TEST(ExecutionSim, CommissionPerTrade) {
    ExecutionSimulator sim({
        .commission_model = CommissionModel::PerTrade,
        .per_trade_flat   = 2.50,
    });
    const Price comm = sim.compute_commission(500, to_price(100.0));
    EXPECT_NEAR(from_price(comm), 2.50, 0.001);
}

// ── Commission: Bps ───────────────────────────────────────────────────────────
TEST(ExecutionSim, CommissionBps) {
    ExecutionSimulator sim({
        .commission_model = CommissionModel::Bps,
        .bps_rate         = 10.0,  // 10 bps = 0.1%
    });
    // notional = $100 × 100 shares = $10,000; commission = $10,000 × 0.001 = $10
    const Price comm = sim.compute_commission(100, to_price(100.0));
    EXPECT_NEAR(from_price(comm), 10.0, 0.01);
}

// ── Slippage: None ────────────────────────────────────────────────────────────
TEST(ExecutionSim, SlippageNone) {
    ExecutionSimulator sim({.slippage_model = SlippageModel::None});
    EXPECT_EQ(sim.model_slippage(Side::Buy, 1000, to_price(100.0)), 0);
}

// ── Slippage: Fixed ───────────────────────────────────────────────────────────
TEST(ExecutionSim, SlippageFixed) {
    ExecutionSimulator sim({
        .slippage_model      = SlippageModel::Fixed,
        .fixed_slippage_bps  = 2.0,  // 2 bps
    });
    const Price slip = sim.model_slippage(Side::Buy, 100, to_price(100.0));
    // Expected: to_price(100) × 2 / 10000 = 0.02 → to_price(0.02)
    EXPECT_NEAR(from_price(slip), 0.02, 0.001);
    EXPECT_GT(slip, 0); // positive = cost
}

// ── Slippage: Linear ─────────────────────────────────────────────────────────
TEST(ExecutionSim, SlippageLinear) {
    ExecutionSimulator sim({
        .slippage_model     = SlippageModel::Linear,
        .linear_impact_bps  = 10.0,  // 10 bps per 100% participation
        .adv_qty            = 1'000'000.0,
    });
    // 1000 / 1M = 0.1% participation → impact = 10 × 0.001 = 0.01 bps
    const Price slip = sim.model_slippage(Side::Buy, 1000, to_price(100.0));
    EXPECT_GT(slip, 0);
}

// ── Slippage: SqrtImpact ─────────────────────────────────────────────────────
TEST(ExecutionSim, SlippageSqrtImpact) {
    ExecutionSimulator sim({
        .slippage_model = SlippageModel::SqrtImpact,
        .sigma          = 0.02,
        .adv_qty        = 1'000'000.0,
    });
    const Price slip1 = sim.model_slippage(Side::Buy, 1000,   to_price(100.0));
    const Price slip2 = sim.model_slippage(Side::Buy, 4000,   to_price(100.0));
    // sqrt impact: slip2 should be ~2× slip1 (4× qty → √4 = 2)
    EXPECT_GT(slip2, slip1);
    const double ratio = static_cast<double>(slip2) / static_cast<double>(slip1);
    EXPECT_NEAR(ratio, 2.0, 0.05);
}

// ── Slippage: BookWalk VWAP ───────────────────────────────────────────────────
TEST(ExecutionSim, SlippageBookWalk) {
    ExecutionSimulator sim({.slippage_model = SlippageModel::BookWalk});

    // Depth: 100 @ 101, 200 @ 102, 300 @ 103
    std::vector<PriceLevel> depth = {
        {.price = to_price(101.0), .total_qty = 100, .order_count = 1},
        {.price = to_price(102.0), .total_qty = 200, .order_count = 1},
        {.price = to_price(103.0), .total_qty = 300, .order_count = 1},
    };

    // Buy 250 shares: 100@101 + 150@102
    // VWAP = (100×101 + 150×102) / 250 = (10100 + 15300) / 250 = 101.6
    const Price arrival = to_price(101.0);
    const Price slip = sim.compute_slippage(Side::Buy, 250, arrival, depth);
    EXPECT_NEAR(from_price(slip), 0.6, 0.001);
    EXPECT_GT(slip, 0); // cost > 0 for buy that walks up
}

// ── Slippage: BookWalk sell (bid walk down) ───────────────────────────────────
TEST(ExecutionSim, SlippageBookWalkSell) {
    ExecutionSimulator sim({.slippage_model = SlippageModel::BookWalk});

    // Bid depth (descending): 100 @ 100, 200 @ 99
    std::vector<PriceLevel> bid_depth = {
        {.price = to_price(100.0), .total_qty = 100, .order_count = 1},
        {.price = to_price(99.0),  .total_qty = 200, .order_count = 1},
    };

    // Sell 200: 100@100 + 100@99; VWAP = (100×100 + 100×99)/200 = 99.5
    // Arrival = 100.0; slippage = 100 - 99.5 = 0.5
    const Price arrival = to_price(100.0);
    const Price slip = sim.compute_slippage(Side::Sell, 200, arrival, bid_depth);
    EXPECT_NEAR(from_price(slip), 0.5, 0.001);
    EXPECT_GT(slip, 0);
}

// ── annotate fill in-place ────────────────────────────────────────────────────
TEST(ExecutionSim, AnnotateFill) {
    ExecutionSimulator sim({
        .slippage_model   = SlippageModel::Fixed,
        .fixed_slippage_bps = 1.0,
        .commission_model = CommissionModel::PerShare,
        .per_share_rate   = 0.01,
    });

    Fill f{};
    f.side       = Side::Buy;
    f.fill_price = to_price(50.0);
    f.fill_qty   = 200;

    sim.annotate(f, to_price(50.0), {});

    EXPECT_GT(f.commission, 0);
    EXPECT_GE(f.slippage, 0);
    // commission = to_price(0.01) × 200 = $2
    EXPECT_NEAR(from_price(f.commission), 2.0, 0.001);
}

// ── Config mutation ───────────────────────────────────────────────────────────
TEST(ExecutionSim, ConfigMutation) {
    ExecutionSimulator sim;
    auto cfg = sim.config();
    cfg.commission_model = CommissionModel::None;
    sim.set_config(cfg);
    EXPECT_EQ(sim.compute_commission(100, to_price(50.0)), 0);
}
