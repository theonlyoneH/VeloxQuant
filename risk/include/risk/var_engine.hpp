#pragma once
// risk/var_engine.hpp  ── Value-at-Risk and Expected Shortfall engine
//
// Maintains a rolling window of P&L returns and provides:
//  • Historical VaR:    percentile of the empirical return distribution.
//  • Parametric VaR:    μ - z_α × σ (normal approximation).
//  • Expected Shortfall (CVaR): mean of the tail beyond the VaR threshold.
//
// Design:
//  • The return window is bounded by max_window to avoid unbounded growth.
//  • On each update(r), the oldest sample is evicted if the window is full.
//  • All queries are O(N log N) due to sorting; not suitable for HFT hot path.
//  • Returns should be fractional (e.g. 0.01 = 1% gain).

#include <cstddef>
#include <vector>

namespace risk {

// ── VaRResult ────────────────────────────────────────────────────────────────
struct VaRResult {
    double var_95          {0.0};  ///< Historical VaR at 95%
    double var_99          {0.0};  ///< Historical VaR at 99%
    double es_95           {0.0};  ///< Expected Shortfall (CVaR) at 95%
    double parametric_var95{0.0};  ///< Normal-approximation VaR at 95%
    double parametric_var99{0.0};  ///< Normal-approximation VaR at 99%
    double mean_return     {0.0};
    double return_stddev   {0.0};
    std::size_t n          {0};
};

// ── VaREngine ─────────────────────────────────────────────────────────────────
class VaREngine {
public:
    explicit VaREngine(std::size_t max_window = 252) noexcept;

    // ── Update ────────────────────────────────────────────────────────────────
    void update(double period_return) noexcept;

    // ── Queries ───────────────────────────────────────────────────────────────

    /// Historical VaR: (1-confidence) lower-tail percentile.
    /// Returns a positive number (loss magnitude).
    [[nodiscard]] double historical_var(double confidence = 0.95) const noexcept;

    /// Parametric (Gaussian) VaR.
    [[nodiscard]] double parametric_var(double confidence = 0.95) const noexcept;

    /// Expected Shortfall (CVaR): mean of returns below the VaR threshold.
    [[nodiscard]] double expected_shortfall(double confidence = 0.95) const noexcept;

    /// Compute all VaR metrics at once.
    [[nodiscard]] VaRResult compute() const noexcept;

    // ── Stats ─────────────────────────────────────────────────────────────────
    [[nodiscard]] double mean()   const noexcept { return mean_;   }
    [[nodiscard]] double stddev() const noexcept { return stddev_; }
    [[nodiscard]] std::size_t window_size() const noexcept {
        return returns_.size();
    }
    [[nodiscard]] bool is_warm(std::size_t min_n = 30) const noexcept {
        return returns_.size() >= min_n;
    }

    void reset() noexcept;

private:
    std::size_t          max_window_;
    std::vector<double>  returns_;    ///< Rolling window of period returns
    double               mean_  {0.0};
    double               var_   {0.0};  ///< Online variance (M2 / n)
    double               m2_    {0.0};  ///< Welford M2 accumulator
    double               stddev_{0.0};

    void update_online_stats(double r) noexcept;

    // Helper: sorted copy for quantile estimation
    [[nodiscard]] std::vector<double> sorted_returns() const;
};

// ── Normal quantile (Beasley-Springer-Moro approximation) ────────────────────
[[nodiscard]] double normal_ppf(double p) noexcept;

} // namespace risk
