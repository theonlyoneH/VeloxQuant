// risk/var_engine.cpp  ── VaREngine implementation

#include "risk/var_engine.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <ranges>

namespace risk {

// ── normal_ppf ────────────────────────────────────────────────────────────────
// Beasley-Springer-Moro rational approximation to the standard normal quantile.
// Accurate to ~5 decimal places over (0, 1).
double normal_ppf(double p) noexcept {
    // Coefficients for the rational approximation
    constexpr double a[] = {-3.969683028665376e+01,  2.209460984245205e+02,
                            -2.759285104469687e+02,   1.383577518672690e+02,
                            -3.066479806614716e+01,   2.506628277459239e+00};
    constexpr double b[] = {-5.447609879822406e+01,  1.615858368580409e+02,
                            -1.556989798598866e+02,   6.680131188771972e+01,
                            -1.328068155288572e+01};
    constexpr double c[] = {-7.784894002430293e-03, -3.223964580411365e-01,
                            -2.400758277161838e+00, -2.549732539343734e+00,
                             4.374664141464968e+00,  2.938163982698783e+00};
    constexpr double d[] = { 7.784695709041462e-03,  3.224671290700398e-01,
                              2.445134137142996e+00,  3.754408661907416e+00};

    const double p_low  = 0.02425;
    const double p_high = 1.0 - p_low;

    double x = 0.0;
    if (p < p_low) {
        const double q = std::sqrt(-2.0 * std::log(p));
        x = (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
            ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
    } else if (p <= p_high) {
        const double q = p - 0.5;
        const double r = q * q;
        x = (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q /
            (((((b[0]*r+b[1])*r+b[2])*r+b[3])*r+b[4])*r+1.0);
    } else {
        const double q = std::sqrt(-2.0 * std::log(1.0 - p));
        x = -(((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
             ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
    }
    return x;
}

// ── Construction ──────────────────────────────────────────────────────────────
VaREngine::VaREngine(std::size_t max_window) noexcept
    : max_window_(max_window)
{
    returns_.reserve(max_window);
}

// ── update_online_stats ───────────────────────────────────────────────────────
// Welford online mean/variance. When the window evicts an old sample we
// use a two-pass correction rather than Welford's decrement (which is
// numerically fragile).  For simplicity we recompute stats from the window.
void VaREngine::update_online_stats(double /*r*/) noexcept {
    // Full recompute from the current window (O(N), bounded by max_window_)
    if (returns_.empty()) { mean_ = 0.0; var_ = 0.0; stddev_ = 0.0; return; }

    const std::size_t n = returns_.size();
    double sum = 0.0, sum2 = 0.0;
    for (double x : returns_) { sum += x; sum2 += x * x; }
    mean_   = sum / static_cast<double>(n);
    var_    = (sum2 / static_cast<double>(n)) - (mean_ * mean_);
    stddev_ = std::sqrt(std::max(var_, 0.0));
}

// ── update ────────────────────────────────────────────────────────────────────
void VaREngine::update(double period_return) noexcept {
    if (returns_.size() >= max_window_)
        returns_.erase(returns_.begin());  // evict oldest (O(N), acceptable for daily)
    returns_.push_back(period_return);
    update_online_stats(period_return);
}

// ── sorted_returns ────────────────────────────────────────────────────────────
std::vector<double> VaREngine::sorted_returns() const {
    std::vector<double> s = returns_;
    std::ranges::sort(s);
    return s;
}

// ── historical_var ────────────────────────────────────────────────────────────
double VaREngine::historical_var(double confidence) const noexcept {
    if (returns_.empty()) return 0.0;
    auto s = sorted_returns();
    const double alpha = 1.0 - confidence;
    const std::size_t idx = static_cast<std::size_t>(alpha * static_cast<double>(s.size()));
    const std::size_t safe = std::min(idx, s.size() - 1);
    return -s[safe]; // Positive = loss
}

// ── parametric_var ────────────────────────────────────────────────────────────
double VaREngine::parametric_var(double confidence) const noexcept {
    if (stddev_ == 0.0) return 0.0;
    // VaR = -(μ + z_α × σ)  where z_α = normal_ppf(1-confidence)
    const double z = normal_ppf(1.0 - confidence);  // negative number (lower tail)
    return -(mean_ + z * stddev_);
}

// ── expected_shortfall ────────────────────────────────────────────────────────
double VaREngine::expected_shortfall(double confidence) const noexcept {
    if (returns_.empty()) return 0.0;
    auto s = sorted_returns();
    const double alpha = 1.0 - confidence;
    const std::size_t cut = static_cast<std::size_t>(
        alpha * static_cast<double>(s.size()));
    if (cut == 0) return -s[0];  // Only the worst observation

    double tail_sum = 0.0;
    for (std::size_t i = 0; i < cut; ++i) tail_sum += s[i];
    return -tail_sum / static_cast<double>(cut);
}

// ── compute ───────────────────────────────────────────────────────────────────
VaRResult VaREngine::compute() const noexcept {
    VaRResult r{};
    r.n              = returns_.size();
    r.mean_return    = mean_;
    r.return_stddev  = stddev_;
    r.var_95         = historical_var(0.95);
    r.var_99         = historical_var(0.99);
    r.es_95          = expected_shortfall(0.95);
    r.parametric_var95 = parametric_var(0.95);
    r.parametric_var99 = parametric_var(0.99);
    return r;
}

// ── reset ─────────────────────────────────────────────────────────────────────
void VaREngine::reset() noexcept {
    returns_.clear();
    mean_   = 0.0;
    var_    = 0.0;
    m2_     = 0.0;
    stddev_ = 0.0;
}

} // namespace risk
