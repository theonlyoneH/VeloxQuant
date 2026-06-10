#pragma once
// research/parameter_scan.hpp  ── Grid search / parameter sweep over strategy params
//
// Usage:
//   ParameterScan scan;
//   scan.add_range("fast_period", {5, 10, 20});
//   scan.add_range("slow_period", {30, 50, 100});
//   for (auto& combo : scan.combinations()) { ... }

#include "research/backtest_result.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace res {

// ── ParameterGrid ─────────────────────────────────────────────────────────────
class ParameterScan {
public:
    /// Add a named parameter with discrete candidate values
    void add_range(std::string name, std::vector<double> values);

    /// Total number of parameter combinations
    [[nodiscard]] std::size_t combination_count() const noexcept;

    /// Generate all parameter combinations as a flat list of maps
    [[nodiscard]]
    std::vector<std::unordered_map<std::string, double>> combinations() const;

    [[nodiscard]] const std::vector<std::string>& param_names() const noexcept {
        return names_;
    }

private:
    std::vector<std::string>              names_;
    std::vector<std::vector<double>>      ranges_;
};

// ── ScanRunner ────────────────────────────────────────────────────────────────
/// Runs a callable for every parameter combination and collects BacktestResults.
using BacktestFn = std::function<BacktestResult(
    const std::unordered_map<std::string, double>& params)>;

struct ScanResult {
    std::vector<BacktestResult> runs;

    /// Return the run with the highest Sharpe ratio
    [[nodiscard]] const BacktestResult* best_by_sharpe() const noexcept;

    /// Return the run with the lowest max drawdown
    [[nodiscard]] const BacktestResult* best_by_drawdown() const noexcept;

    /// Sort runs in-place by Sharpe (descending)
    void sort_by_sharpe();
};

/// Execute a full parameter scan synchronously
[[nodiscard]] ScanResult run_scan(const ParameterScan& grid, BacktestFn fn);

} // namespace res
