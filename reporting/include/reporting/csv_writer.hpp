#pragma once
// reporting/csv_writer.hpp  ── CSV export for equity curves and backtest results
//
// Zero external dependencies.  Uses std::ofstream with comma-separated values,
// proper quoting for strings containing commas or quotes.

#include "research/backtest_result.hpp"

#include <filesystem>
#include <ostream>
#include <string_view>
#include <vector>

namespace rpt {

using res::BacktestResult;

// ── CsvWriter ─────────────────────────────────────────────────────────────────
class CsvWriter {
public:
    // ── Equity curve ──────────────────────────────────────────────────────────

    /// Write an equity curve to `path`.
    /// Columns: timestamp_ns, nav
    static bool write_equity_curve(const port::EquityCurve& curve,
                                   const std::filesystem::path& path);

    /// Write equity curve to an already-open stream.
    static void write_equity_curve(const port::EquityCurve& curve,
                                   std::ostream& out);

    // ── BacktestResult ────────────────────────────────────────────────────────

    /// Write a single BacktestResult's metrics + trade stats.
    static bool write_result(const BacktestResult& r,
                             const std::filesystem::path& path);

    /// Write multiple BacktestResults as one CSV (for scan comparisons).
    static bool write_results(const std::vector<BacktestResult>& results,
                              const std::filesystem::path& path);

    static void write_results(const std::vector<BacktestResult>& results,
                              std::ostream& out);

    // ── Returns series ────────────────────────────────────────────────────────

    /// Export the period return series derived from an equity curve.
    static bool write_returns(const port::EquityCurve& curve,
                              const std::filesystem::path& path);

private:
    /// Quote a string field (wrap in "" if contains comma or newline)
    static std::string quote(std::string_view s);
};

} // namespace rpt
