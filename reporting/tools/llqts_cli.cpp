// reporting/tools/llqts_cli.cpp  ── Main CLI entry point
//
// llqts_cli --config run.yaml --output ./reports [--html] [--csv]
//
// This is a thin driver; real strategy/backtest execution would be wired in
// via the research layer in a full integration.

#include "reporting/cli.hpp"
#include "reporting/config.hpp"
#include "reporting/csv_writer.hpp"
#include "reporting/html_report.hpp"
#include "research/backtest_result.hpp"

#include <filesystem>
#include <iostream>

int main(int argc, char* argv[]) {
    rpt::CliParser cli("llqts_cli",
        "Low-Latency Quantitative Trading Simulation Platform");

    cli.flag("--help",    "-h", "Print this help message and exit");
    cli.flag("--html",    "-H", "Generate HTML report");
    cli.flag("--csv",     "-C", "Generate CSV exports");
    cli.flag("--verbose", "-v", "Verbose output");

    cli.option("--config", "-c", "FILE", "YAML config file (default: llqts.yaml)");
    cli.option("--output", "-o", "DIR",  "Output directory (default: ./reports)");
    cli.option("--strategy", "-s", "NAME","Strategy to run: sma|mean_reversion|momentum");

    auto args = cli.parse(argc, argv);

    if (args.flag("--help")) {
        cli.print_help();
        return 0;
    }

    if (args.has_errors()) {
        for (const auto& e : args.errors())
            std::cerr << "Error: " << e << '\n';
        return 1;
    }

    // ── Load config ───────────────────────────────────────────────────────────
    rpt::Config cfg;
    const auto cfg_path = args.option("--config", "llqts.yaml");
    if (!cfg.load_file(cfg_path) && args.flag("--verbose"))
        std::cout << "[warn] Config file '" << cfg_path << "' not found; using defaults\n";

    const auto out_dir = std::filesystem::path(args.option("--output", "reports"));
    std::filesystem::create_directories(out_dir);

    const auto strategy = args.option("--strategy",
        cfg.get_string("strategy.name", "sma"));

    if (args.flag("--verbose"))
        std::cout << "[info] Strategy: " << strategy << "\n"
                  << "[info] Output:   " << out_dir  << "\n";

    // ── Build a synthetic BacktestResult for demonstration ────────────────────
    res::BacktestResult result;
    result.strategy_name   = strategy;
    result.run_id          = "cli-demo-001";
    result.initial_capital = cfg.get_double("backtest.initial_capital", 1'000'000.0);

    // Synthetic equity curve: 252 daily points, 8% annual drift
    {
        double nav = result.initial_capital;
        for (int day = 0; day <= 252; ++day) {
            nav *= (day == 0) ? 1.0 : 1.000304;  // ≈8% p.a. / 252
            result.equity_curve.push_back({
                static_cast<md::Timestamp>(day) * 86'400'000'000'000LL,
                nav
            });
        }
    }

    result.total_trades   = 47;
    result.winning_trades = 29;
    result.losing_trades  = 18;
    result.avg_win        = 4800.0;
    result.avg_loss       = -2200.0;
    res::compute(result, 0.05, 252.0);

    // ── Emit reports ──────────────────────────────────────────────────────────
    if (args.flag("--html") || (!args.flag("--csv"))) {
        // Default: HTML
        const auto html_path = out_dir / (strategy + "_report.html");
        if (rpt::HtmlReport::write(result, html_path))
            std::cout << "[ok] HTML report → " << html_path.string() << '\n';
        else
            std::cerr << "[err] Failed to write HTML report\n";
    }

    if (args.flag("--csv")) {
        const auto eq_path  = out_dir / (strategy + "_equity.csv");
        const auto met_path = out_dir / (strategy + "_metrics.csv");
        if (rpt::CsvWriter::write_equity_curve(result.equity_curve, eq_path))
            std::cout << "[ok] Equity CSV  → " << eq_path.string()  << '\n';
        if (rpt::CsvWriter::write_result(result, met_path))
            std::cout << "[ok] Metrics CSV → " << met_path.string() << '\n';
    }

    return 0;
}
