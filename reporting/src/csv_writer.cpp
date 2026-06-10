// reporting/csv_writer.cpp
#include "reporting/csv_writer.hpp"
#include "portfolio/performance.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace rpt {

// ── quote ─────────────────────────────────────────────────────────────────────
std::string CsvWriter::quote(std::string_view s) {
    if (s.find_first_of(",\"\n\r") == std::string_view::npos)
        return std::string(s);
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char c : s) {
        if (c == '"') out += '"';
        out += c;
    }
    out += '"';
    return out;
}

// ── write_equity_curve (stream) ───────────────────────────────────────────────
void CsvWriter::write_equity_curve(const port::EquityCurve& curve,
                                    std::ostream& out) {
    out << "timestamp_ns,nav\n";
    for (const auto& pt : curve)
        out << pt.timestamp << ',' << std::fixed << std::setprecision(6)
            << pt.nav << '\n';
}

// ── write_equity_curve (file) ─────────────────────────────────────────────────
bool CsvWriter::write_equity_curve(const port::EquityCurve& curve,
                                    const std::filesystem::path& path) {
    std::ofstream f{path};
    if (!f.is_open()) return false;
    write_equity_curve(curve, f);
    return true;
}

// ── write_result ──────────────────────────────────────────────────────────────
bool CsvWriter::write_result(const BacktestResult& r,
                              const std::filesystem::path& path) {
    std::ofstream f{path};
    if (!f.is_open()) return false;

    const auto& m = r.metrics;
    f << "metric,value\n"
      << "strategy,"        << quote(r.strategy_name)                                << '\n'
      << "total_return,"    << std::fixed << std::setprecision(6) << m.total_return  << '\n'
      << "annualised_return,"<< m.annualised_return                                  << '\n'
      << "sharpe_ratio,"    << m.sharpe_ratio                                        << '\n'
      << "sortino_ratio,"   << m.sortino_ratio                                       << '\n'
      << "calmar_ratio,"    << m.calmar_ratio                                        << '\n'
      << "max_drawdown,"    << m.max_drawdown                                        << '\n'
      << "volatility,"      << m.volatility                                          << '\n'
      << "var_95,"          << m.var_95                                              << '\n'
      << "var_99,"          << m.var_99                                              << '\n'
      << "total_trades,"    << r.total_trades                                        << '\n'
      << "win_rate,"        << r.win_rate                                            << '\n'
      << "profit_factor,"   << r.profit_factor                                       << '\n';
    return true;
}

// ── write_results (stream) ────────────────────────────────────────────────────
void CsvWriter::write_results(const std::vector<BacktestResult>& results,
                               std::ostream& out) {
    out << "strategy,run_id,total_return,annualised_return,sharpe,sortino,"
           "calmar,max_drawdown,volatility,var_95,var_99,win_rate,profit_factor\n";

    for (const auto& r : results) {
        const auto& m = r.metrics;
        out << std::fixed << std::setprecision(6)
            << quote(r.strategy_name) << ','
            << quote(r.run_id)        << ','
            << m.total_return         << ','
            << m.annualised_return    << ','
            << m.sharpe_ratio         << ','
            << m.sortino_ratio        << ','
            << m.calmar_ratio         << ','
            << m.max_drawdown         << ','
            << m.volatility           << ','
            << m.var_95               << ','
            << m.var_99               << ','
            << r.win_rate             << ','
            << r.profit_factor        << '\n';
    }
}

// ── write_results (file) ──────────────────────────────────────────────────────
bool CsvWriter::write_results(const std::vector<BacktestResult>& results,
                               const std::filesystem::path& path) {
    std::ofstream f{path};
    if (!f.is_open()) return false;
    write_results(results, f);
    return true;
}

// ── write_returns ─────────────────────────────────────────────────────────────
bool CsvWriter::write_returns(const port::EquityCurve& curve,
                               const std::filesystem::path& path) {
    std::ofstream f{path};
    if (!f.is_open()) return false;
    const auto returns = port::to_returns(curve);
    f << "period,return\n";
    for (std::size_t i = 0; i < returns.size(); ++i)
        f << i << ',' << std::fixed << std::setprecision(8) << returns[i] << '\n';
    return true;
}

} // namespace rpt
