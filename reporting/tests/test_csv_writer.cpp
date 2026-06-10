// reporting/tests/test_csv_writer.cpp
#include "reporting/csv_writer.hpp"
#include <gtest/gtest.h>
#include <sstream>

using namespace rpt;
using port::EquityCurve;

namespace {
EquityCurve make_curve() {
    return {{0, 100'000.0}, {1, 101'000.0}, {2, 99'000.0}};
}

res::BacktestResult make_result() {
    res::BacktestResult r;
    r.strategy_name  = "SMA";
    r.run_id         = "test-001";
    r.equity_curve   = make_curve();
    r.total_trades   = 10;
    r.winning_trades = 6;
    r.losing_trades  = 4;
    r.win_rate       = 0.6;
    r.profit_factor  = 1.5;
    r.metrics.sharpe_ratio = 1.2;
    r.metrics.max_drawdown = 0.15;
    return r;
}
} // anon

TEST(CsvWriter, EquityCurveHeaders) {
    std::ostringstream ss;
    CsvWriter::write_equity_curve(make_curve(), ss);
    const auto s = ss.str();
    EXPECT_NE(s.find("timestamp_ns"), std::string::npos);
    EXPECT_NE(s.find("nav"),          std::string::npos);
}

TEST(CsvWriter, EquityCurveRowCount) {
    std::ostringstream ss;
    CsvWriter::write_equity_curve(make_curve(), ss);
    // 1 header + 3 data rows = 4 newlines
    int lines = 0;
    for (char c : ss.str()) if (c == '\n') ++lines;
    EXPECT_EQ(lines, 4);
}

TEST(CsvWriter, EmptyCurveHeaderOnly) {
    std::ostringstream ss;
    CsvWriter::write_equity_curve(EquityCurve{}, ss);
    int lines = 0;
    for (char c : ss.str()) if (c == '\n') ++lines;
    EXPECT_EQ(lines, 1);  // just the header
}

TEST(CsvWriter, ResultsHeaders) {
    std::ostringstream ss;
    CsvWriter::write_results({make_result()}, ss);
    const auto s = ss.str();
    EXPECT_NE(s.find("sharpe"),       std::string::npos);
    EXPECT_NE(s.find("max_drawdown"), std::string::npos);
    EXPECT_NE(s.find("win_rate"),     std::string::npos);
}

TEST(CsvWriter, ResultsStrategyName) {
    std::ostringstream ss;
    CsvWriter::write_results({make_result()}, ss);
    EXPECT_NE(ss.str().find("SMA"), std::string::npos);
}

TEST(CsvWriter, MultipleResultsRowCount) {
    std::ostringstream ss;
    CsvWriter::write_results({make_result(), make_result()}, ss);
    int lines = 0;
    for (char c : ss.str()) if (c == '\n') ++lines;
    EXPECT_EQ(lines, 3);  // header + 2 data rows
}

TEST(CsvWriter, QuoteFieldWithComma) {
    // Test internal: a strategy name with a comma should be quoted
    res::BacktestResult r = make_result();
    r.strategy_name = "SMA, fast";
    std::ostringstream ss;
    CsvWriter::write_results({r}, ss);
    EXPECT_NE(ss.str().find('"'), std::string::npos);
}
