#pragma once
// research/backtest_result.hpp  ── Aggregated result of one backtest run
//
// BacktestResult stores all outputs produced by running a strategy over a
// historical dataset.  It is the primary data type consumed by the reporting
// and research layers.

#include "portfolio/performance.hpp"
#include "analytics/stats.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace res {

using port::PerformanceMetrics;
using port::EquityCurve;

// ── BacktestResult ────────────────────────────────────────────────────────────
struct BacktestResult {
    // ── Identity ──────────────────────────────────────────────────────────────
    std::string strategy_name;
    std::string run_id;                       ///< Unique run identifier (e.g. UUID-lite)
    int64_t     start_ts  {0};                ///< Backtest start timestamp (ns)
    int64_t     end_ts    {0};                ///< Backtest end timestamp (ns)

    // ── Parameters ────────────────────────────────────────────────────────────
    std::unordered_map<std::string, double> params;   ///< Strategy param snapshot

    // ── Equity curve ──────────────────────────────────────────────────────────
    EquityCurve equity_curve;
    double      initial_capital {1'000'000.0};

    // ── Computed metrics ──────────────────────────────────────────────────────
    PerformanceMetrics metrics;

    // ── Trade statistics ──────────────────────────────────────────────────────
    uint64_t total_trades  {0};
    uint64_t winning_trades{0};
    uint64_t losing_trades {0};
    double   win_rate      {0.0};   ///< winning / total
    double   avg_win       {0.0};   ///< Average winning trade P&L
    double   avg_loss      {0.0};   ///< Average losing trade P&L (negative)
    double   profit_factor {0.0};   ///< |sum wins| / |sum losses|

    // ── Signal/execution latency ──────────────────────────────────────────────
    anlt::DescriptiveStats signal_latency_ns;   ///< Strategy compute latency
    anlt::DescriptiveStats fill_latency_ns;     ///< Simulated fill latency

    // ── Helpers ───────────────────────────────────────────────────────────────
    [[nodiscard]] double total_return() const noexcept {
        return metrics.total_return;
    }
    [[nodiscard]] bool is_valid() const noexcept {
        return !strategy_name.empty() && !equity_curve.empty();
    }
};

/// Fill metrics from an equity curve (delegates to port::compute_metrics)
void compute(BacktestResult& r,
             double risk_free_rate   = 0.0,
             double periods_per_year = 252.0);

} // namespace res
