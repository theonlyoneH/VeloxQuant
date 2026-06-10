// tests/test_var_engine.cpp  ── VaREngine unit tests
#include "risk/var_engine.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <numeric>
#include <vector>

using namespace risk;

namespace {

/// Feed N identical returns into the engine
void feed_n(VaREngine& e, double r, int n) {
    for (int i = 0; i < n; ++i) e.update(r);
}

} // anon

// ── Empty engine ──────────────────────────────────────────────────────────────
TEST(VaREngine, EmptyEngine) {
    VaREngine e(100);
    EXPECT_EQ(e.window_size(), 0u);
    EXPECT_NEAR(e.historical_var(0.95), 0.0, 1e-9);
    EXPECT_NEAR(e.parametric_var(0.95), 0.0, 1e-9);
    EXPECT_NEAR(e.expected_shortfall(0.95), 0.0, 1e-9);
    EXPECT_FALSE(e.is_warm(30));
}

// ── Window size bounded by max_window ─────────────────────────────────────────
TEST(VaREngine, WindowBounded) {
    VaREngine e(10);
    feed_n(e, 0.01, 20);  // feed 20 but window = 10
    EXPECT_EQ(e.window_size(), 10u);
}

// ── Mean is correct for constant returns ──────────────────────────────────────
TEST(VaREngine, MeanConstant) {
    VaREngine e(100);
    feed_n(e, 0.05, 50);
    EXPECT_NEAR(e.mean(), 0.05, 1e-9);
    EXPECT_NEAR(e.stddev(), 0.0, 1e-6);
}

// ── Historical VaR: known distribution ───────────────────────────────────────
TEST(VaREngine, HistoricalVaR_KnownDist) {
    VaREngine e(200);
    // 190 returns of +0.01, 10 returns of -0.10
    feed_n(e, 0.01, 190);
    feed_n(e, -0.10, 10);

    // At 95%: lower 5% = 10 observations out of 200 → all -0.10
    const double v95 = e.historical_var(0.95);
    EXPECT_NEAR(v95, 0.10, 0.01);  // should be ~10% loss
}

// ── Historical VaR 99 ≥ VaR 95 ────────────────────────────────────────────────
TEST(VaREngine, VaR99GeqVaR95) {
    VaREngine e(200);
    for (int i = 0; i < 200; ++i)
        e.update(-static_cast<double>(i) * 0.0005); // uniformly varying returns

    EXPECT_GE(e.historical_var(0.99), e.historical_var(0.95));
}

// ── Expected Shortfall ≥ Historical VaR ──────────────────────────────────────
TEST(VaREngine, ES_GtEq_VaR) {
    VaREngine e(100);
    // Uniform losses in [-0.1, 0]
    for (int i = 0; i < 100; ++i)
        e.update(-static_cast<double>(i) * 0.001);

    EXPECT_GE(e.expected_shortfall(0.95), e.historical_var(0.95));
}

// ── Parametric VaR: constant returns → 0 stddev → 0 VaR ──────────────────────
TEST(VaREngine, ParametricVaR_ZeroStddev) {
    VaREngine e(50);
    feed_n(e, 0.01, 50);
    EXPECT_NEAR(e.parametric_var(0.95), 0.0, 1e-6);
}

// ── Parametric VaR for known N(0, σ) distribution ────────────────────────────
TEST(VaREngine, ParametricVaR_KnownGaussian) {
    // A N(0, 0.01) distribution → parametric VaR(95%) ≈ 1.645 × 0.01 = 0.01645
    VaREngine e(10000);
    // Approximate by alternating +σ and -σ returns → mean≈0, std≈σ
    const double sigma = 0.01;
    for (int i = 0; i < 5000; ++i) {
        e.update( sigma);
        e.update(-sigma);
    }

    const double pvar = e.parametric_var(0.95);
    EXPECT_NEAR(pvar, 1.645 * sigma, 0.005);  // tolerance ±0.5%
}

// ── normal_ppf: known quantiles ───────────────────────────────────────────────
TEST(VaREngine, NormalPPF) {
    EXPECT_NEAR(normal_ppf(0.5),   0.0,    1e-3);
    EXPECT_NEAR(normal_ppf(0.975), 1.96,   0.01);
    EXPECT_NEAR(normal_ppf(0.025),-1.96,   0.01);
    EXPECT_NEAR(normal_ppf(0.95),  1.6449, 0.01);
    EXPECT_NEAR(normal_ppf(0.99),  2.3263, 0.01);
    // Symmetry
    EXPECT_NEAR(normal_ppf(0.01), -normal_ppf(0.99), 1e-4);
}

// ── compute() returns consistent values ───────────────────────────────────────
TEST(VaREngine, ComputeAllAtOnce) {
    VaREngine e(100);
    for (int i = 0; i < 100; ++i)
        e.update(-static_cast<double>(i) * 0.001);

    const auto r = e.compute();
    EXPECT_EQ(r.n, 100u);
    EXPECT_GE(r.var_99, r.var_95);
    EXPECT_GE(r.es_95,  r.var_95);
    EXPECT_GE(r.parametric_var99, r.parametric_var95);
}

// ── reset clears all state ────────────────────────────────────────────────────
TEST(VaREngine, ResetClearsState) {
    VaREngine e(50);
    feed_n(e, 0.05, 30);
    EXPECT_GT(e.window_size(), 0u);

    e.reset();
    EXPECT_EQ(e.window_size(), 0u);
    EXPECT_NEAR(e.mean(), 0.0, 1e-9);
    EXPECT_NEAR(e.stddev(), 0.0, 1e-9);
}

// ── is_warm threshold ────────────────────────────────────────────────────────
TEST(VaREngine, IsWarm) {
    VaREngine e(100);
    EXPECT_FALSE(e.is_warm(30));
    feed_n(e, 0.01, 29);
    EXPECT_FALSE(e.is_warm(30));
    e.update(0.01);
    EXPECT_TRUE(e.is_warm(30));
}

// ── Window eviction keeps rolling ────────────────────────────────────────────
TEST(VaREngine, WindowEviction) {
    VaREngine e(10);
    // Fill with -0.01
    feed_n(e, -0.01, 10);
    EXPECT_NEAR(e.mean(), -0.01, 1e-9);

    // Replace all with +0.02 by evicting old entries
    feed_n(e, 0.02, 10);
    EXPECT_NEAR(e.mean(), 0.02, 1e-9);
}
