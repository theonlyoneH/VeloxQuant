#pragma once
// analytics/stats.hpp  ── Descriptive statistics for latency / return series
//
// All functions operate on contiguous ranges (std::span<const double>).
// No heap allocation in the hot path.

#include <algorithm>
#include <cmath>
#include <numeric>
#include <span>
#include <vector>

namespace anlt {

// ── DescriptiveStats ──────────────────────────────────────────────────────────
struct DescriptiveStats {
    double min    {0.0};
    double max    {0.0};
    double mean   {0.0};
    double median {0.0};
    double stddev {0.0};
    double p50    {0.0};
    double p90    {0.0};
    double p95    {0.0};
    double p99    {0.0};
    double p999   {0.0};
    std::size_t n {0};
};

// ── Free functions ────────────────────────────────────────────────────────────

[[nodiscard]] double mean(std::span<const double> v) noexcept;
[[nodiscard]] double variance(std::span<const double> v) noexcept;  // population
[[nodiscard]] double stddev(std::span<const double> v) noexcept;
[[nodiscard]] double percentile(std::vector<double> sorted, double p) noexcept;

/// Compute full descriptive stats from a (possibly unsorted) sample.
[[nodiscard]] DescriptiveStats describe(std::vector<double> samples);

/// Geometric mean of 1+returns (for CAGR-like analysis)
[[nodiscard]] double geo_mean(std::span<const double> returns) noexcept;

/// Skewness (third standardised moment)
[[nodiscard]] double skewness(std::span<const double> v) noexcept;

/// Excess kurtosis (fourth standardised moment − 3)
[[nodiscard]] double kurtosis(std::span<const double> v) noexcept;

} // namespace anlt
