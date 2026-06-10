// reporting/html_report.cpp  ── Self-contained HTML report generation
#include "reporting/html_report.hpp"
#include "portfolio/performance.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace rpt {

// ── helpers ───────────────────────────────────────────────────────────────────
static std::string fmt(double v, int prec = 4) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(prec) << v;
    return ss.str();
}

static std::string pct(double v, int prec = 2) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(prec) << (v * 100.0) << '%';
    return ss.str();
}

// ── render_head ───────────────────────────────────────────────────────────────
std::string HtmlReport::render_head(std::string_view title) {
    return std::string(R"(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>)") + std::string(title) + R"(</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:'Segoe UI',system-ui,sans-serif;background:#0f172a;color:#e2e8f0;padding:2rem}
  h1{font-size:1.8rem;color:#38bdf8;margin-bottom:1.5rem}
  h2{font-size:1.2rem;color:#94a3b8;margin:2rem 0 0.8rem}
  .card{background:#1e293b;border-radius:12px;padding:1.5rem;margin-bottom:1.5rem;
        box-shadow:0 4px 24px rgba(0,0,0,.4)}
  table{width:100%;border-collapse:collapse;font-size:.9rem}
  th{text-align:left;color:#64748b;font-weight:600;padding:.5rem .8rem;
     border-bottom:1px solid #334155}
  td{padding:.5rem .8rem;border-bottom:1px solid #1e293b}
  tr:last-child td{border:0}
  .pos{color:#4ade80} .neg{color:#f87171} .neu{color:#e2e8f0}
  canvas{max-height:360px}
  .grid2{display:grid;grid-template-columns:1fr 1fr;gap:1.5rem}
  @media(max-width:720px){.grid2{grid-template-columns:1fr}}
</style>
</head>
<body>
)";
}

// ── render_metrics_table ──────────────────────────────────────────────────────
std::string HtmlReport::render_metrics_table(const BacktestResult& r) {
    const auto& m = r.metrics;
    auto row = [](std::string_view label, std::string value, std::string css = "neu") {
        return "<tr><td>" + std::string(label) + "</td><td class='" + css
             + "'>" + value + "</td></tr>\n";
    };

    auto cls = [](double v) -> std::string { return v >= 0 ? "pos" : "neg"; };

    return "<table>\n"
        + row("Strategy",            r.strategy_name)
        + row("Total Return",        pct(m.total_return),        cls(m.total_return))
        + row("Annualised Return",   pct(m.annualised_return),   cls(m.annualised_return))
        + row("Sharpe Ratio",        fmt(m.sharpe_ratio),        cls(m.sharpe_ratio))
        + row("Sortino Ratio",       fmt(m.sortino_ratio),       cls(m.sortino_ratio))
        + row("Calmar Ratio",        fmt(m.calmar_ratio),        cls(m.calmar_ratio))
        + row("Max Drawdown",        pct(m.max_drawdown),        m.max_drawdown > 0.2 ? "neg" : "neu")
        + row("Volatility (ann.)",   pct(m.volatility))
        + row("VaR 95%",             pct(m.var_95),              "neg")
        + row("VaR 99%",             pct(m.var_99),              "neg")
        + row("CVaR 95%",            pct(m.expected_shortfall_95), "neg")
        + row("N Returns",           std::to_string(m.n_returns))
        + "</table>\n";
}

// ── render_trade_table ────────────────────────────────────────────────────────
std::string HtmlReport::render_trade_table(const BacktestResult& r) {
    auto row = [](std::string_view label, std::string value) {
        return "<tr><td>" + std::string(label) + "</td><td>" + value + "</td></tr>\n";
    };
    return "<table>\n"
        + row("Total Trades",   std::to_string(r.total_trades))
        + row("Winning",        std::to_string(r.winning_trades))
        + row("Losing",         std::to_string(r.losing_trades))
        + row("Win Rate",       pct(r.win_rate))
        + row("Avg Win",        fmt(r.avg_win, 2))
        + row("Avg Loss",       fmt(r.avg_loss, 2))
        + row("Profit Factor",  fmt(r.profit_factor))
        + "</table>\n";
}

// ── equity JSON helpers ───────────────────────────────────────────────────────
std::string HtmlReport::equity_to_json_labels(const port::EquityCurve& c) {
    std::string out = "[";
    for (std::size_t i = 0; i < c.size(); ++i) {
        if (i) out += ',';
        out += std::to_string(i);
    }
    return out + "]";
}

std::string HtmlReport::equity_to_json_data(const port::EquityCurve& c) {
    std::string out = "[";
    for (std::size_t i = 0; i < c.size(); ++i) {
        if (i) out += ',';
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << c[i].nav;
        out += ss.str();
    }
    return out + "]";
}

// ── render_equity_chart ────────────────────────────────────────────────────────
std::string HtmlReport::render_equity_chart(const BacktestResult& r) {
    if (r.equity_curve.empty()) return "";
    return R"(<canvas id="eqChart"></canvas>
<script>
new Chart(document.getElementById('eqChart'),{
  type:'line',
  data:{
    labels:)" + equity_to_json_labels(r.equity_curve) + R"(,
    datasets:[{
      label:'NAV',
      data:)" + equity_to_json_data(r.equity_curve) + R"(,
      borderColor:'#38bdf8',borderWidth:2,pointRadius:0,fill:true,
      backgroundColor:'rgba(56,189,248,.08)'
    }]
  },
  options:{
    animation:false,
    plugins:{legend:{display:false}},
    scales:{
      x:{display:false},
      y:{grid:{color:'#1e293b'},ticks:{color:'#64748b'}}
    }
  }
});
</script>
)";
}

// ── render_scan_table ─────────────────────────────────────────────────────────
std::string HtmlReport::render_scan_table(const ScanResult& scan) {
    if (scan.runs.empty()) return "<p>No results.</p>";
    std::string out = "<table>\n<tr>"
        "<th>Strategy</th><th>Run ID</th><th>Sharpe</th><th>Sortino</th>"
        "<th>Max DD</th><th>Total Return</th></tr>\n";
    for (const auto& r : scan.runs) {
        const auto& m = r.metrics;
        out += "<tr><td>" + r.strategy_name + "</td><td>" + r.run_id + "</td>"
            + "<td>" + fmt(m.sharpe_ratio)    + "</td>"
            + "<td>" + fmt(m.sortino_ratio)   + "</td>"
            + "<td>" + pct(m.max_drawdown)    + "</td>"
            + "<td>" + pct(m.total_return)    + "</td></tr>\n";
    }
    return out + "</table>\n";
}

// ── render_footer ──────────────────────────────────────────────────────────────
std::string HtmlReport::render_footer() {
    return "<p style='margin-top:2rem;color:#475569;font-size:.75rem'>"
           "Generated by LLQTSimPlatform &mdash; FR-10 Reporting</p>\n"
           "</body></html>\n";
}

// ── write (stream) ────────────────────────────────────────────────────────────
void HtmlReport::write(const BacktestResult& r, std::ostream& out) {
    out << render_head("Backtest Report – " + r.strategy_name)
        << "<h1>Backtest Report — " << r.strategy_name << "</h1>\n"
        << "<div class='card'>\n"
        << "<h2>Equity Curve</h2>\n"
        << render_equity_chart(r)
        << "</div>\n"
        << "<div class='grid2'>\n"
        << "<div class='card'><h2>Performance Metrics</h2>\n"
        << render_metrics_table(r) << "</div>\n"
        << "<div class='card'><h2>Trade Statistics</h2>\n"
        << render_trade_table(r)   << "</div>\n"
        << "</div>\n"
        << render_footer();
}

// ── write (file) ──────────────────────────────────────────────────────────────
bool HtmlReport::write(const BacktestResult& r,
                        const std::filesystem::path& path) {
    std::ofstream f{path};
    if (!f.is_open()) return false;
    write(r, f);
    return true;
}

// ── write_scan (stream) ───────────────────────────────────────────────────────
void HtmlReport::write_scan(const ScanResult& scan,
                             std::string_view title,
                             std::ostream& out) {
    out << render_head(title)
        << "<h1>" << title << "</h1>\n"
        << "<div class='card'>\n"
        << "<h2>Parameter Scan Results</h2>\n"
        << render_scan_table(scan)
        << "</div>\n"
        << render_footer();
}

// ── write_scan (file) ─────────────────────────────────────────────────────────
bool HtmlReport::write_scan(const ScanResult& scan,
                             std::string_view title,
                             const std::filesystem::path& path) {
    std::ofstream f{path};
    if (!f.is_open()) return false;
    write_scan(scan, title, f);
    return true;
}

} // namespace rpt
