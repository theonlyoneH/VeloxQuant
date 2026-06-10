// reporting/tests/test_html_report.cpp
#include "reporting/html_report.hpp"
#include <gtest/gtest.h>
#include <sstream>

using namespace rpt;

namespace {
res::BacktestResult make_result() {
    res::BacktestResult r;
    r.strategy_name  = "Momentum";
    r.run_id         = "html-test-001";
    r.equity_curve   = {{0, 100'000.0}, {1, 105'000.0}, {2, 103'000.0}};
    r.total_trades   = 20;
    r.winning_trades = 14;
    r.losing_trades  = 6;
    r.win_rate       = 0.7;
    r.profit_factor  = 2.1;
    r.metrics.sharpe_ratio    = 1.8;
    r.metrics.sortino_ratio   = 2.5;
    r.metrics.max_drawdown    = 0.10;
    r.metrics.total_return    = 0.08;
    r.metrics.annualised_return = 0.08;
    r.metrics.volatility      = 0.12;
    r.metrics.var_95          = 0.015;
    r.metrics.var_99          = 0.025;
    return r;
}
} // anon

TEST(HtmlReport, ContainsDoctypeHeader) {
    std::ostringstream ss;
    HtmlReport::write(make_result(), ss);
    EXPECT_NE(ss.str().find("<!DOCTYPE html>"), std::string::npos);
}

TEST(HtmlReport, ContainsStrategyName) {
    std::ostringstream ss;
    HtmlReport::write(make_result(), ss);
    EXPECT_NE(ss.str().find("Momentum"), std::string::npos);
}

TEST(HtmlReport, ContainsChartJs) {
    std::ostringstream ss;
    HtmlReport::write(make_result(), ss);
    EXPECT_NE(ss.str().find("chart.js"), std::string::npos);
}

TEST(HtmlReport, ContainsMetrics) {
    std::ostringstream ss;
    HtmlReport::write(make_result(), ss);
    const auto s = ss.str();
    EXPECT_NE(s.find("Sharpe"),     std::string::npos);
    EXPECT_NE(s.find("Sortino"),    std::string::npos);
    EXPECT_NE(s.find("Drawdown"),   std::string::npos);
    EXPECT_NE(s.find("Volatility"), std::string::npos);
}

TEST(HtmlReport, ContainsEquityData) {
    std::ostringstream ss;
    HtmlReport::write(make_result(), ss);
    // The equity curve values should appear in the JS data array
    EXPECT_NE(ss.str().find("100000"), std::string::npos);
}

TEST(HtmlReport, ContainsTradeStats) {
    std::ostringstream ss;
    HtmlReport::write(make_result(), ss);
    const auto s = ss.str();
    EXPECT_NE(s.find("Win Rate"),      std::string::npos);
    EXPECT_NE(s.find("Profit Factor"), std::string::npos);
}

TEST(HtmlReport, EmptyEquityCurveOk) {
    res::BacktestResult r = make_result();
    r.equity_curve.clear();
    std::ostringstream ss;
    // Should not crash / throw
    EXPECT_NO_THROW(HtmlReport::write(r, ss));
    EXPECT_NE(ss.str().find("<!DOCTYPE"), std::string::npos);
}

TEST(HtmlReport, ScanReport) {
    res::ScanResult scan;
    for (int i = 0; i < 3; ++i) {
        auto r = make_result();
        r.run_id = "run-" + std::to_string(i);
        r.metrics.sharpe_ratio = static_cast<double>(i);
        scan.runs.push_back(r);
    }
    std::ostringstream ss;
    HtmlReport::write_scan(scan, "Scan Report", ss);
    const auto s = ss.str();
    EXPECT_NE(s.find("Scan Report"),   std::string::npos);
    EXPECT_NE(s.find("run-0"),         std::string::npos);
    EXPECT_NE(s.find("run-2"),         std::string::npos);
}
