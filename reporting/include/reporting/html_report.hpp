#pragma once
// reporting/html_report.hpp  ── Self-contained HTML report generator
//
// Generates a single-file HTML report embedding:
//   • Chart.js (via CDN) for the equity curve chart
//   • Metrics summary table
//   • Trade statistics table
//   • Parameter scan results table (if provided)
// No external C++ libraries required.

#include "research/backtest_result.hpp"
#include "research/parameter_scan.hpp"

#include <filesystem>
#include <ostream>
#include <vector>

namespace rpt {

using res::BacktestResult;
using res::ScanResult;

// ── HtmlReport ────────────────────────────────────────────────────────────────
class HtmlReport {
public:
    // ── Single backtest ───────────────────────────────────────────────────────

    /// Write a full HTML report for one BacktestResult to a file.
    static bool write(const BacktestResult& r,
                      const std::filesystem::path& path);

    /// Write report to an ostream (for testing / piping).
    static void write(const BacktestResult& r, std::ostream& out);

    // ── Scan comparison ───────────────────────────────────────────────────────

    /// Write a comparison report for multiple backtest runs (scan result).
    static bool write_scan(const ScanResult& scan,
                           std::string_view title,
                           const std::filesystem::path& path);

    static void write_scan(const ScanResult& scan,
                           std::string_view title,
                           std::ostream& out);

private:
    // ── Private HTML generation helpers ──────────────────────────────────────

    static std::string render_head(std::string_view title);
    static std::string render_metrics_table(const BacktestResult& r);
    static std::string render_trade_table(const BacktestResult& r);
    static std::string render_equity_chart(const BacktestResult& r);
    static std::string render_scan_table(const ScanResult& scan);
    static std::string render_footer();

    // Convert equity curve to JSON arrays for Chart.js
    static std::string equity_to_json_labels(const port::EquityCurve& c);
    static std::string equity_to_json_data(const port::EquityCurve& c);
};

} // namespace rpt
