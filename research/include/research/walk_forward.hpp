#pragma once
// research/walk_forward.hpp  ── Walk-forward analysis
//
// Splits a dataset into rolling in-sample / out-of-sample windows:
//   [train_start ─────── train_end][test_start ── test_end]
//           ↓ slide by step_size
//       [train_start ─────── train_end][test_start ── test_end]
//
// Caller provides a train_fn (optimise → return best params)
// and a test_fn (run with params → return BacktestResult).

#include "research/backtest_result.hpp"

#include <functional>
#include <vector>

namespace res {

// ── WalkForwardConfig ─────────────────────────────────────────────────────────
struct WalkForwardConfig {
    std::size_t train_bars {252};   ///< In-sample window length (bars)
    std::size_t test_bars  {63};    ///< Out-of-sample window length (bars)
    std::size_t step_bars  {21};    ///< How far to slide each iteration
    std::size_t total_bars {0};     ///< Full dataset length; computed from data if 0
};

// ── Window ────────────────────────────────────────────────────────────────────
struct WalkForwardWindow {
    std::size_t train_start;
    std::size_t train_end;    ///< exclusive
    std::size_t test_start;
    std::size_t test_end;     ///< exclusive
    std::size_t fold_idx;
};

// ── WalkForwardResult ─────────────────────────────────────────────────────────
struct WalkForwardResult {
    std::vector<WalkForwardWindow>  windows;
    std::vector<BacktestResult>     oos_results;   ///< One per window

    /// Concatenated OOS equity curve (stitched together)
    [[nodiscard]] port::EquityCurve stitched_equity() const;

    /// Aggregate metrics over all OOS windows
    [[nodiscard]] PerformanceMetrics aggregate_metrics(
        double risk_free_rate   = 0.0,
        double periods_per_year = 252.0) const;
};

using TrainFn = std::function<
    std::unordered_map<std::string, double>(
        std::size_t train_start, std::size_t train_end)>;

using TestFn  = std::function<
    BacktestResult(
        const std::unordered_map<std::string, double>& params,
        std::size_t test_start, std::size_t test_end)>;

/// Generate all walk-forward windows for a dataset of `total_bars` bars.
[[nodiscard]] std::vector<WalkForwardWindow>
make_windows(const WalkForwardConfig& cfg, std::size_t total_bars);

/// Run a full walk-forward analysis.
[[nodiscard]] WalkForwardResult
run_walk_forward(const WalkForwardConfig& cfg,
                 std::size_t total_bars,
                 TrainFn train_fn,
                 TestFn  test_fn);

} // namespace res
