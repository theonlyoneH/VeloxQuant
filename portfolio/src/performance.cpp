// portfolio/performance.cpp  ── Performance metrics implementation

#include "portfolio/performance.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numeric>
#include <ranges>
#include <stdexcept>

namespace port {

// ── to_returns ────────────────────────────────────────────────────────────────
std::vector<double> to_returns(const EquityCurve& curve) noexcept {
    if (curve.size() < 2) return {};
    std::vector<double> ret;
    ret.reserve(curve.size() - 1);
    for (std::size_t i = 1; i < curve.size(); ++i) {
        const double prev = curve[i - 1].nav;
        if (prev == 0.0) { ret.push_back(0.0); continue; }
        ret.push_back((curve[i].nav - prev) / prev);
    }
    return ret;
}

// ── max_drawdown ──────────────────────────────────────────────────────────────
double max_drawdown(std::span<const double> nav_series) noexcept {
    if (nav_series.size() < 2) return 0.0;
    double peak     = nav_series[0];
    double max_dd   = 0.0;
    for (double nav : nav_series) {
        if (nav > peak) peak = nav;
        const double dd = (peak > 0.0) ? (peak - nav) / peak : 0.0;
        if (dd > max_dd) max_dd = dd;
    }
    return max_dd;
}

// ── sharpe_ratio ─────────────────────────────────────────────────────────────
double sharpe_ratio(std::span<const double> returns,
                    double rf_per_period,
                    double sqrt_periods) noexcept {
    if (returns.empty()) return 0.0;
    const std::size_t n = returns.size();

    // Excess returns
    double sum = 0.0, sum2 = 0.0;
    for (double r : returns) {
        const double ex = r - rf_per_period;
        sum  += ex;
        sum2 += ex * ex;
    }
    const double mean = sum / static_cast<double>(n);
    const double var  = (sum2 / static_cast<double>(n)) - (mean * mean);
    const double sd   = std::sqrt(std::max(var, 0.0));
    if (sd == 0.0) return 0.0;
    return (mean / sd) * sqrt_periods;
}

// ── sortino_ratio ────────────────────────────────────────────────────────────
double sortino_ratio(std::span<const double> returns,
                     double rf_per_period,
                     double sqrt_periods) noexcept {
    if (returns.empty()) return 0.0;
    const std::size_t n = returns.size();

    double sum       = 0.0;
    double down_sum2 = 0.0;
    std::size_t n_down = 0;

    for (double r : returns) {
        const double ex = r - rf_per_period;
        sum += ex;
        if (ex < 0.0) {
            down_sum2 += ex * ex;
            ++n_down;
        }
    }

    const double mean = sum / static_cast<double>(n);

    // Downside deviation over all n (not just negative periods – standard convention)
    const double down_var = down_sum2 / static_cast<double>(n);
    const double down_sd  = std::sqrt(std::max(down_var, 0.0));

    if (down_sd == 0.0) return (mean >= 0.0) ? std::numeric_limits<double>::infinity() : 0.0;
    return (mean / down_sd) * sqrt_periods;
}

// ── historical_var ────────────────────────────────────────────────────────────
double historical_var(std::vector<double> returns, double confidence) noexcept {
    if (returns.empty()) return 0.0;
    // Sort ascending; loss is at the lower tail
    std::ranges::sort(returns);
    const double alpha       = 1.0 - confidence;
    const std::size_t idx    = static_cast<std::size_t>(alpha * static_cast<double>(returns.size()));
    const std::size_t safe   = std::min(idx, returns.size() - 1);
    // VaR is reported as a positive number (loss magnitude)
    return -returns[safe];
}

// ── expected_shortfall ────────────────────────────────────────────────────────
double expected_shortfall(std::vector<double> returns, double confidence) noexcept {
    if (returns.empty()) return 0.0;
    std::ranges::sort(returns);
    const double alpha    = 1.0 - confidence;
    const std::size_t cut = static_cast<std::size_t>(alpha * static_cast<double>(returns.size()));
    if (cut == 0) return (returns.empty() ? 0.0 : -returns[0]);
    double tail_sum = 0.0;
    for (std::size_t i = 0; i < cut; ++i) tail_sum += returns[i];
    return -tail_sum / static_cast<double>(cut);
}

// ── compute_metrics ───────────────────────────────────────────────────────────
PerformanceMetrics compute_metrics(const EquityCurve& curve,
                                    double risk_free_rate,
                                    double periods_per_year) {
    PerformanceMetrics m{};
    if (curve.size() < 2) return m;

    // Extract NAV series and returns
    std::vector<double> navs;
    navs.reserve(curve.size());
    for (const auto& pt : curve) navs.push_back(pt.nav);

    const auto returns   = to_returns(curve);
    m.n_returns          = returns.size();

    if (returns.empty()) return m;

    // Total return
    const double initial = navs.front();
    const double final_v = navs.back();
    if (initial <= 0.0) return m;
    m.total_return = (final_v - initial) / initial;

    // Annualised return (CAGR)
    const double n_years = static_cast<double>(returns.size()) / periods_per_year;
    m.annualised_return  = (n_years > 0.0)
        ? (std::pow(1.0 + m.total_return, 1.0 / n_years) - 1.0)
        : 0.0;

    // Risk-free rate per period
    const double rf_per_period = risk_free_rate / periods_per_year;
    const double sqrt_periods  = std::sqrt(periods_per_year);

    // Volatility
    {
        double sum = 0.0, sum2 = 0.0;
        for (double r : returns) { sum += r; sum2 += r * r; }
        const double mean = sum / static_cast<double>(returns.size());
        const double var  = (sum2 / static_cast<double>(returns.size())) - mean * mean;
        m.volatility = std::sqrt(std::max(var, 0.0)) * sqrt_periods;
    }

    // Sharpe & Sortino
    m.sharpe_ratio  = sharpe_ratio(returns, rf_per_period, sqrt_periods);
    m.sortino_ratio = sortino_ratio(returns, rf_per_period, sqrt_periods);

    // Downside volatility (used by sortino internally – re-derive for exposure)
    {
        double dsum2 = 0.0;
        for (double r : returns) {
            const double ex = r - rf_per_period;
            if (ex < 0.0) dsum2 += ex * ex;
        }
        m.downside_volatility = std::sqrt(std::max(dsum2 / static_cast<double>(returns.size()), 0.0))
                              * sqrt_periods;
    }

    // Max Drawdown
    m.max_drawdown     = max_drawdown(navs);
    m.max_drawdown_pct = m.max_drawdown;

    // Calmar
    m.calmar_ratio = (m.max_drawdown > 0.0)
        ? (m.annualised_return / m.max_drawdown)
        : 0.0;

    // VaR
    m.var_95 = historical_var(returns, 0.95);
    m.var_99 = historical_var(returns, 0.99);
    m.expected_shortfall_95 = expected_shortfall(returns, 0.95);

    return m;
}

} // namespace port
