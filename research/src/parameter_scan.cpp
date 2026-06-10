// research/parameter_scan.cpp
#include "research/parameter_scan.hpp"
#include <algorithm>
#include <cassert>
#include <numeric>

namespace res {

// ── ParameterScan ─────────────────────────────────────────────────────────────
void ParameterScan::add_range(std::string name, std::vector<double> values) {
    assert(!values.empty());
    names_.push_back(std::move(name));
    ranges_.push_back(std::move(values));
}

std::size_t ParameterScan::combination_count() const noexcept {
    if (ranges_.empty()) return 0;
    std::size_t n = 1;
    for (const auto& r : ranges_) n *= r.size();
    return n;
}

std::vector<std::unordered_map<std::string, double>>
ParameterScan::combinations() const {
    std::vector<std::unordered_map<std::string, double>> result;
    const std::size_t total = combination_count();
    if (total == 0) return result;
    result.reserve(total);

    // Iterate via odometer / mixed-radix counter
    std::vector<std::size_t> idx(names_.size(), 0);
    for (std::size_t combo = 0; combo < total; ++combo) {
        std::unordered_map<std::string, double> params;
        for (std::size_t i = 0; i < names_.size(); ++i)
            params[names_[i]] = ranges_[i][idx[i]];
        result.push_back(std::move(params));

        // Increment odometer (last dimension varies fastest)
        for (int d = static_cast<int>(names_.size()) - 1; d >= 0; --d) {
            if (++idx[d] < ranges_[d].size()) break;
            idx[d] = 0;
        }
    }
    return result;
}

// ── ScanResult ────────────────────────────────────────────────────────────────
const BacktestResult* ScanResult::best_by_sharpe() const noexcept {
    if (runs.empty()) return nullptr;
    auto it = std::max_element(runs.begin(), runs.end(),
        [](const BacktestResult& a, const BacktestResult& b) {
            return a.metrics.sharpe_ratio < b.metrics.sharpe_ratio;
        });
    return &*it;
}

const BacktestResult* ScanResult::best_by_drawdown() const noexcept {
    if (runs.empty()) return nullptr;
    auto it = std::min_element(runs.begin(), runs.end(),
        [](const BacktestResult& a, const BacktestResult& b) {
            return a.metrics.max_drawdown < b.metrics.max_drawdown;
        });
    return &*it;
}

void ScanResult::sort_by_sharpe() {
    std::ranges::sort(runs, [](const BacktestResult& a, const BacktestResult& b) {
        return a.metrics.sharpe_ratio > b.metrics.sharpe_ratio;
    });
}

// ── run_scan ──────────────────────────────────────────────────────────────────
ScanResult run_scan(const ParameterScan& grid, BacktestFn fn) {
    ScanResult sr;
    for (const auto& params : grid.combinations()) {
        auto r = fn(params);
        // Attach params to result
        r.params = params;
        sr.runs.push_back(std::move(r));
    }
    return sr;
}

} // namespace res
