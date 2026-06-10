// research/tests/test_parameter_scan.cpp
#include "research/parameter_scan.hpp"
#include <gtest/gtest.h>

using namespace res;

TEST(ParameterScan, EmptyGridZeroCombinations) {
    ParameterScan ps;
    EXPECT_EQ(ps.combination_count(), 0u);
    EXPECT_TRUE(ps.combinations().empty());
}

TEST(ParameterScan, SingleParam) {
    ParameterScan ps;
    ps.add_range("x", {1.0, 2.0, 3.0});
    EXPECT_EQ(ps.combination_count(), 3u);
    auto combos = ps.combinations();
    ASSERT_EQ(combos.size(), 3u);
    EXPECT_NEAR(combos[0].at("x"), 1.0, 1e-9);
    EXPECT_NEAR(combos[1].at("x"), 2.0, 1e-9);
    EXPECT_NEAR(combos[2].at("x"), 3.0, 1e-9);
}

TEST(ParameterScan, TwoParamsCross) {
    ParameterScan ps;
    ps.add_range("a", {1.0, 2.0});
    ps.add_range("b", {10.0, 20.0, 30.0});
    EXPECT_EQ(ps.combination_count(), 6u);  // 2×3
    auto combos = ps.combinations();
    ASSERT_EQ(combos.size(), 6u);
    // Verify all (a,b) combinations are present
    std::set<std::pair<double,double>> seen;
    for (const auto& c : combos)
        seen.insert({c.at("a"), c.at("b")});
    EXPECT_EQ(seen.size(), 6u);
}

TEST(ParameterScan, ThreeParamsCross) {
    ParameterScan ps;
    ps.add_range("p", {5.0, 10.0});
    ps.add_range("q", {30.0, 60.0});
    ps.add_range("r", {0.01, 0.02, 0.03});
    EXPECT_EQ(ps.combination_count(), 12u);  // 2×2×3
}

TEST(ParameterScan, RunScan) {
    ParameterScan ps;
    ps.add_range("fast", {5.0, 10.0});
    ps.add_range("slow", {20.0, 40.0});

    // Dummy backtest: Sharpe = fast / slow
    ScanResult sr = run_scan(ps, [](const auto& params) {
        BacktestResult r;
        r.strategy_name = "TestStrat";
        r.params = params;
        r.metrics.sharpe_ratio = params.at("fast") / params.at("slow");
        r.equity_curve.push_back({0, 100'000.0});
        return r;
    });

    EXPECT_EQ(sr.runs.size(), 4u);
}

TEST(ParameterScan, BestBySharpe) {
    ParameterScan ps;
    ps.add_range("x", {1.0, 2.0, 3.0});

    ScanResult sr = run_scan(ps, [](const auto& params) {
        BacktestResult r;
        r.metrics.sharpe_ratio = params.at("x");
        r.equity_curve.push_back({0, 100.0});
        return r;
    });

    const auto* best = sr.best_by_sharpe();
    ASSERT_NE(best, nullptr);
    EXPECT_NEAR(best->metrics.sharpe_ratio, 3.0, 1e-9);
}

TEST(ParameterScan, BestByDrawdown) {
    ParameterScan ps;
    ps.add_range("x", {0.1, 0.3, 0.05});

    ScanResult sr = run_scan(ps, [](const auto& params) {
        BacktestResult r;
        r.metrics.max_drawdown = params.at("x");
        r.equity_curve.push_back({0, 100.0});
        return r;
    });

    const auto* best = sr.best_by_drawdown();
    ASSERT_NE(best, nullptr);
    EXPECT_NEAR(best->metrics.max_drawdown, 0.05, 1e-9);
}

TEST(ParameterScan, SortBySharpe) {
    ParameterScan ps;
    ps.add_range("x", {3.0, 1.0, 2.0});

    ScanResult sr = run_scan(ps, [](const auto& params) {
        BacktestResult r;
        r.metrics.sharpe_ratio = params.at("x");
        r.equity_curve.push_back({0, 100.0});
        return r;
    });

    sr.sort_by_sharpe();
    EXPECT_GE(sr.runs[0].metrics.sharpe_ratio,
              sr.runs[1].metrics.sharpe_ratio);
    EXPECT_GE(sr.runs[1].metrics.sharpe_ratio,
              sr.runs[2].metrics.sharpe_ratio);
}
