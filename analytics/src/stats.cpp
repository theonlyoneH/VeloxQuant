// analytics/stats.cpp
#include "analytics/stats.hpp"
#include <cassert>
#include <cmath>
#include <numeric>

namespace anlt {

double mean(std::span<const double> v) noexcept {
    if (v.empty()) return 0.0;
    return std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
}

double variance(std::span<const double> v) noexcept {
    if (v.size() < 2) return 0.0;
    double m = mean(v);
    double s = 0.0;
    for (double x : v) s += (x - m) * (x - m);
    return s / static_cast<double>(v.size());   // population variance
}

double stddev(std::span<const double> v) noexcept {
    return std::sqrt(variance(v));
}

double percentile(std::vector<double> sorted, double p) noexcept {
    if (sorted.empty()) return 0.0;
    // sorted must already be in ascending order
    const double idx = p * static_cast<double>(sorted.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(idx);
    const std::size_t hi = std::min(lo + 1, sorted.size() - 1);
    const double frac = idx - static_cast<double>(lo);
    return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
}

DescriptiveStats describe(std::vector<double> samples) {
    DescriptiveStats s{};
    s.n = samples.size();
    if (s.n == 0) return s;

    std::ranges::sort(samples);
    s.min    = samples.front();
    s.max    = samples.back();
    s.mean   = mean(samples);
    s.stddev = stddev(samples);
    s.p50    = s.median = percentile(samples, 0.50);
    s.p90    = percentile(samples, 0.90);
    s.p95    = percentile(samples, 0.95);
    s.p99    = percentile(samples, 0.99);
    s.p999   = percentile(samples, 0.999);
    return s;
}

double geo_mean(std::span<const double> returns) noexcept {
    if (returns.empty()) return 0.0;
    double log_sum = 0.0;
    for (double r : returns) {
        if (1.0 + r <= 0.0) return 0.0;  // guard log(0)
        log_sum += std::log(1.0 + r);
    }
    return std::exp(log_sum / static_cast<double>(returns.size())) - 1.0;
}

double skewness(std::span<const double> v) noexcept {
    if (v.size() < 3) return 0.0;
    double m = mean(v);
    double sd = stddev(v);
    if (sd == 0.0) return 0.0;
    double s = 0.0;
    for (double x : v) {
        double z = (x - m) / sd;
        s += z * z * z;
    }
    return s / static_cast<double>(v.size());
}

double kurtosis(std::span<const double> v) noexcept {
    if (v.size() < 4) return 0.0;
    double m = mean(v);
    double sd = stddev(v);
    if (sd == 0.0) return 0.0;
    double s = 0.0;
    for (double x : v) {
        double z = (x - m) / sd;
        s += z * z * z * z;
    }
    return s / static_cast<double>(v.size()) - 3.0;  // excess kurtosis
}

} // namespace anlt
