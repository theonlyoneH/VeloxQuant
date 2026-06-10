// tests/test_performance.cpp  ── Performance metrics unit tests
#include "portfolio/performance.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <numeric>

using namespace port;

namespace {

/// Build a simple equity curve from a vector of NAV values
EquityCurve make_curve(const std::vector<double>& navs) {
    EquityCurve c;
    c.reserve(navs.size());
    for (std::size_t i = 0; i < navs.size(); ++i)
        c.push_back({static_cast<md::Timestamp>(i), navs[i]});
    return c;
}

} // anon

// ── to_returns ────────────────────────────────────────────────────────────────
TEST(Performance, ToReturns_Empty) {
    EXPECT_TRUE(to_returns({}).empty());
}

TEST(Performance, ToReturns_KnownValues) {
    auto curve = make_curve({100.0, 105.0, 100.0, 110.0});
    auto r     = to_returns(curve);
    ASSERT_EQ(r.size(), 3u);
    EXPECT_NEAR(r[0],  0.05,   1e-9);  // 100→105: +5%
    EXPECT_NEAR(r[1], -0.04761904762, 1e-7); // 105→100: -4.76%
    EXPECT_NEAR(r[2],  0.10,   1e-9);  // 100→110: +10%
}

// ── max_drawdown ──────────────────────────────────────────────────────────────
TEST(Performance, MaxDrawdown_None) {
    // Monotonically increasing → no drawdown
    std::vector<double> navs = {100, 110, 120, 130};
    EXPECT_NEAR(max_drawdown(navs), 0.0, 1e-9);
}

TEST(Performance, MaxDrawdown_SingleDip) {
    // Peak 120, trough 90: DD = (120-90)/120 = 25%
    std::vector<double> navs = {100.0, 120.0, 90.0, 95.0};
    EXPECT_NEAR(max_drawdown(navs), (120.0-90.0)/120.0, 1e-9);
}

TEST(Performance, MaxDrawdown_50Pct) {
    std::vector<double> navs = {200.0, 100.0};
    EXPECT_NEAR(max_drawdown(navs), 0.50, 1e-9);
}

TEST(Performance, MaxDrawdown_MultiPeak) {
    // Peak 150, trough 60 → DD=60%  (larger than the first dip)
    std::vector<double> navs = {100.0, 130.0, 110.0, 150.0, 60.0};
    EXPECT_NEAR(max_drawdown(navs), (150.0-60.0)/150.0, 1e-9);
}

// ── sharpe_ratio ─────────────────────────────────────────────────────────────
TEST(Performance, SharpeRatio_ZeroReturn) {
    std::vector<double> returns(252, 0.0);
    EXPECT_NEAR(sharpe_ratio(returns, 0.0, std::sqrt(252.0)), 0.0, 1e-9);
}

TEST(Performance, SharpeRatio_PositiveMean) {
    // Constant +1% daily returns, no RF: Sharpe = mean/sd → sd=0 → 0
    // (pure zero-variance means divide-by-zero → 0)
    std::vector<double> returns(252, 0.01);
    EXPECT_NEAR(sharpe_ratio(returns, 0.0, std::sqrt(252.0)), 0.0, 1e-6);
}

TEST(Performance, SharpeRatio_KnownValue) {
    // Alternating +0.02 and -0.01 returns: mean=0.005, var=0.000225, sd≈0.015
    // Sharpe (daily, no RF) ≈ 0.005/0.015 × √252 ≈ 5.29
    std::vector<double> returns;
    for (int i = 0; i < 252; ++i)
        returns.push_back((i % 2 == 0) ? 0.02 : -0.01);

    const double sr = sharpe_ratio(returns, 0.0, std::sqrt(252.0));
    EXPECT_GT(sr, 3.0);   // should be positive and significant
    EXPECT_LT(sr, 10.0);
}

// ── sortino_ratio ─────────────────────────────────────────────────────────────
TEST(Performance, SortinoRatio_AllPositive) {
    // No negative returns → downside stddev = 0 → infinity
    std::vector<double> returns(10, 0.01);
    const double sortino = sortino_ratio(returns, 0.0, std::sqrt(252.0));
    EXPECT_TRUE(std::isinf(sortino) || sortino > 100.0);
}

TEST(Performance, SortinoRatio_HigherThanSharpe) {
    // Strategy with upside volatility but low downside: Sortino > Sharpe
    std::vector<double> returns;
    for (int i = 0; i < 100; ++i)
        returns.push_back((i % 10 == 0) ? -0.005 : 0.02);  // 90% up, 10% small down

    const double sharpe  = sharpe_ratio(returns, 0.0, std::sqrt(252.0));
    const double sortino_ = sortino_ratio(returns, 0.0, std::sqrt(252.0));
    EXPECT_GT(sortino_, sharpe);
}

// ── historical_var ────────────────────────────────────────────────────────────
TEST(Performance, HistoricalVaR_95) {
    // 100 returns: 95 of +0.01, 5 of -0.10
    std::vector<double> returns(95, 0.01);
    for (int i = 0; i < 5; ++i) returns.push_back(-0.10);

    const double v95 = historical_var(returns, 0.95);
    EXPECT_GT(v95, 0.0);  // positive = loss
    EXPECT_NEAR(v95, 0.10, 0.01);  // approximately the -10% tail
}

TEST(Performance, HistoricalVaR_99_LargerThan_95) {
    std::vector<double> returns(100);
    for (int i = 0; i < 100; ++i)
        returns[i] = -static_cast<double>(i) * 0.001;  // 0, -0.001, ..., -0.099

    const double v95 = historical_var(returns, 0.95);
    const double v99 = historical_var(returns, 0.99);
    EXPECT_GE(v99, v95);
}

// ── expected_shortfall ────────────────────────────────────────────────────────
TEST(Performance, ExpectedShortfall_GtVaR) {
    std::vector<double> returns(100);
    for (int i = 0; i < 100; ++i)
        returns[i] = -static_cast<double>(i) * 0.001;

    const double v95  = historical_var(returns, 0.95);
    const double es95 = expected_shortfall(returns, 0.95);
    EXPECT_GE(es95, v95);  // ES ≥ VaR always
}

// ── compute_metrics ───────────────────────────────────────────────────────────
TEST(Performance, ComputeMetrics_EmptyCurve) {
    const auto m = compute_metrics({});
    EXPECT_NEAR(m.sharpe_ratio, 0.0, 1e-9);
    EXPECT_NEAR(m.max_drawdown, 0.0, 1e-9);
}

TEST(Performance, ComputeMetrics_FlatEquity) {
    auto curve = make_curve({100'000.0, 100'000.0, 100'000.0});
    const auto m = compute_metrics(curve, 0.0, 252.0);
    EXPECT_NEAR(m.total_return, 0.0, 1e-9);
    EXPECT_NEAR(m.sharpe_ratio, 0.0, 1e-6);
    EXPECT_NEAR(m.max_drawdown, 0.0, 1e-9);
    EXPECT_EQ(m.n_returns, 2u);
}

TEST(Performance, ComputeMetrics_PositiveTrend) {
    // NAV grows 1% per day for 252 days
    std::vector<double> navs;
    navs.push_back(100'000.0);
    for (int i = 0; i < 252; ++i)
        navs.push_back(navs.back() * 1.001);

    const auto m = compute_metrics(make_curve(navs), 0.0, 252.0);
    EXPECT_GT(m.total_return, 0.0);
    EXPECT_GT(m.annualised_return, 0.0);
    EXPECT_NEAR(m.max_drawdown, 0.0, 1e-6);  // monotone → no drawdown
    EXPECT_EQ(m.n_returns, 252u);
}

TEST(Performance, ComputeMetrics_MaxDrawdown) {
    // Sharp 50% drawdown in the middle
    auto curve = make_curve({100'000.0, 150'000.0, 75'000.0, 80'000.0});
    const auto m = compute_metrics(curve, 0.0, 252.0);
    EXPECT_NEAR(m.max_drawdown, (150'000.0-75'000.0)/150'000.0, 1e-6);
}

TEST(Performance, ComputeMetrics_CalmarRatio) {
    // Simple 2-point curve: 100k → 110k  (no drawdown → calmar = 0)
    auto curve = make_curve({100'000.0, 110'000.0});
    const auto m = compute_metrics(curve, 0.0, 252.0);
    // calmar = annualised_return / max_drawdown; max_dd=0 → calmar=0
    EXPECT_NEAR(m.calmar_ratio, 0.0, 1e-9);
}

TEST(Performance, ComputeMetrics_VaR_NonZero) {
    // Generate a realistic return series with some losses
    std::vector<double> navs;
    navs.push_back(100'000.0);
    // 200 daily returns: 180 of +0.005, 20 of -0.03
    for (int i = 0; i < 200; ++i) {
        const double r = (i % 10 == 0) ? -0.03 : 0.005;
        navs.push_back(navs.back() * (1.0 + r));
    }
    const auto m = compute_metrics(make_curve(navs), 0.0, 252.0);
    EXPECT_GT(m.var_95, 0.0);
    EXPECT_GT(m.var_99, 0.0);
    EXPECT_GE(m.var_99, m.var_95);
    EXPECT_GE(m.expected_shortfall_95, m.var_95);
}
