#pragma once
// portfolio/performance.hpp  ── Performance metrics computed from an equity curve
//
// Metrics:
//  Sharpe   = mean(r - rf) / stddev(r) × √periods_per_year
//  Sortino  = mean(r - rf) / downside_stddev(r) × √periods_per_year
//  Calmar   = annualised_return / |max_drawdown|
//  Max DD   = max peak-to-trough percentage decline in NAV
//  VaR(α)   = historical percentile at (1-α) of the return distribution
//
// All return series are derived from consecutive NAV differences:
//   r_i = (NAV_i - NAV_{i-1}) / NAV_{i-1}
//
// Assumptions:
//  • The equity curve has at least 2 points; single-point → metrics are 0.
//  • periods_per_year is the number of return observations per year
//    (e.g. 252 for daily, 52 for weekly, 12 for monthly).

#include "portfolio/portfolio.hpp"

#include <cmath>
#include <span>
#include <vector>

namespace port {

// ── PerformanceMetrics ────────────────────────────────────────────────────────
struct PerformanceMetrics {
    double total_return        {0.0};  ///< (final_nav - initial_nav) / initial_nav
    double annualised_return   {0.0};  ///< CAGR over the period
    double sharpe_ratio        {0.0};
    double sortino_ratio       {0.0};
    double calmar_ratio        {0.0};
    double max_drawdown        {0.0};  ///< Positive value: 0.20 = 20% max dd
    double max_drawdown_pct    {0.0};  ///< Same but stored as fraction
    double var_95              {0.0};  ///< 5th percentile of return dist (loss)
    double var_99              {0.0};  ///< 1st percentile of return dist (loss)
    double expected_shortfall_95{0.0}; ///< CVaR at 95%
    double volatility          {0.0};  ///< Annualised stddev of returns
    double downside_volatility {0.0};  ///< Annualised stddev of negative returns
    std::size_t n_returns      {0};    ///< Number of return observations
};

// ── Compute ───────────────────────────────────────────────────────────────────

/// Compute all performance metrics from an equity curve.
/// @param curve         Vector of {timestamp, nav} pairs (chronological order).
/// @param risk_free     Annualised risk-free rate (e.g. 0.05 for 5%).
/// @param periods       Number of return periods per year (e.g. 252 for daily).
[[nodiscard]]
PerformanceMetrics compute_metrics(const EquityCurve& curve,
                                   double risk_free_rate = 0.0,
                                   double periods_per_year = 252.0);

// ── Individual metrics (for targeted use) ────────────────────────────────────

[[nodiscard]] double max_drawdown(std::span<const double> nav_series) noexcept;

[[nodiscard]] double sharpe_ratio(std::span<const double> returns,
                                  double rf_per_period,
                                  double sqrt_periods) noexcept;

[[nodiscard]] double sortino_ratio(std::span<const double> returns,
                                   double rf_per_period,
                                   double sqrt_periods) noexcept;

[[nodiscard]] double historical_var(std::vector<double> returns,
                                    double confidence) noexcept;

[[nodiscard]] double expected_shortfall(std::vector<double> returns,
                                        double confidence) noexcept;

// ── Utility ───────────────────────────────────────────────────────────────────

/// Convert equity curve to a vector of period returns.
[[nodiscard]] std::vector<double> to_returns(const EquityCurve& curve) noexcept;

} // namespace port
