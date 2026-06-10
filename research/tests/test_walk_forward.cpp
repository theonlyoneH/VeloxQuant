// research/tests/test_walk_forward.cpp
#include "research/walk_forward.hpp"
#include <gtest/gtest.h>

using namespace res;

// ── make_windows ──────────────────────────────────────────────────────────────
TEST(WalkForward, NoWindowsIfTooShort) {
    WalkForwardConfig cfg{.train_bars=100, .test_bars=50, .step_bars=25};
    auto ws = make_windows(cfg, 100);  // not enough for even one window
    EXPECT_TRUE(ws.empty());
}

TEST(WalkForward, ExactOneWindow) {
    WalkForwardConfig cfg{.train_bars=100, .test_bars=50, .step_bars=25};
    auto ws = make_windows(cfg, 150);  // exactly 100+50
    ASSERT_EQ(ws.size(), 1u);
    EXPECT_EQ(ws[0].train_start, 0u);
    EXPECT_EQ(ws[0].train_end,   100u);
    EXPECT_EQ(ws[0].test_start,  100u);
    EXPECT_EQ(ws[0].test_end,    150u);
    EXPECT_EQ(ws[0].fold_idx,    0u);
}

TEST(WalkForward, MultipleWindows) {
    WalkForwardConfig cfg{.train_bars=100, .test_bars=50, .step_bars=25};
    // 300 bars → windows starting at 0, 25, 50, 75, 100, 125
    auto ws = make_windows(cfg, 300);
    EXPECT_GE(ws.size(), 4u);
    // Each consecutive window shifts by step_bars
    for (std::size_t i = 1; i < ws.size(); ++i)
        EXPECT_EQ(ws[i].train_start, ws[i-1].train_start + 25u);
}

TEST(WalkForward, FoldIndexMonotone) {
    WalkForwardConfig cfg{.train_bars=50, .test_bars=20, .step_bars=20};
    auto ws = make_windows(cfg, 300);
    for (std::size_t i = 0; i < ws.size(); ++i)
        EXPECT_EQ(ws[i].fold_idx, i);
}

TEST(WalkForward, TrainEndEqualsTestStart) {
    WalkForwardConfig cfg{.train_bars=100, .test_bars=30, .step_bars=30};
    auto ws = make_windows(cfg, 500);
    for (const auto& w : ws)
        EXPECT_EQ(w.train_end, w.test_start);
}

// ── run_walk_forward ──────────────────────────────────────────────────────────
TEST(WalkForward, RunProducesOneResultPerWindow) {
    WalkForwardConfig cfg{.train_bars=50, .test_bars=20, .step_bars=20};
    const std::size_t total = 200;

    int train_calls = 0, test_calls = 0;

    auto result = run_walk_forward(
        cfg, total,
        [&](std::size_t, std::size_t) -> std::unordered_map<std::string,double> {
            ++train_calls;
            return {{"x", 1.0}};
        },
        [&](const auto&, std::size_t ts, std::size_t te) -> BacktestResult {
            ++test_calls;
            BacktestResult r;
            r.strategy_name = "WF";
            r.equity_curve.push_back({static_cast<int64_t>(ts), 100'000.0});
            r.equity_curve.push_back({static_cast<int64_t>(te), 101'000.0});
            return r;
        });

    EXPECT_EQ(result.windows.size(), result.oos_results.size());
    EXPECT_EQ(train_calls, test_calls);
    EXPECT_GT(train_calls, 0);
}

TEST(WalkForward, StitchedEquityNonEmpty) {
    WalkForwardConfig cfg{.train_bars=50, .test_bars=20, .step_bars=20};

    auto result = run_walk_forward(
        cfg, 200,
        [](std::size_t, std::size_t) { return std::unordered_map<std::string,double>{}; },
        [](const auto&, std::size_t ts, std::size_t te) {
            BacktestResult r;
            r.equity_curve.push_back({static_cast<int64_t>(ts), 100'000.0});
            r.equity_curve.push_back({static_cast<int64_t>(te), 102'000.0});
            return r;
        });

    auto curve = result.stitched_equity();
    EXPECT_FALSE(curve.empty());
}

TEST(WalkForward, AggregateMetrics) {
    WalkForwardConfig cfg{.train_bars=10, .test_bars=5, .step_bars=5};

    auto result = run_walk_forward(
        cfg, 100,
        [](std::size_t, std::size_t) { return std::unordered_map<std::string,double>{}; },
        [](const auto&, std::size_t ts, std::size_t te) {
            BacktestResult r;
            double nav = 100'000.0;
            for (std::size_t i = ts; i <= te; ++i) {
                r.equity_curve.push_back({static_cast<int64_t>(i), nav});
                nav *= 1.001;
            }
            return r;
        });

    auto m = result.aggregate_metrics();
    // Positive return from growing NAV
    EXPECT_GT(m.total_return, 0.0);
}
