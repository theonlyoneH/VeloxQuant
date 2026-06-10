// reporting/tests/test_cli.cpp
#include "reporting/cli.hpp"
#include <gtest/gtest.h>

using rpt::CliParser;
using rpt::ParsedArgs;

namespace {

ParsedArgs parse(const CliParser& cli, std::vector<const char*> args) {
    return cli.parse(static_cast<int>(args.size()), args.data());
}

} // anon

TEST(Cli, NoArgsOk) {
    CliParser cli("prog");
    auto r = parse(cli, {"prog"});
    EXPECT_FALSE(r.has_errors());
    EXPECT_TRUE(r.positional().empty());
}

TEST(Cli, FlagLong) {
    CliParser cli("prog");
    cli.flag("--help", "-h", "Show help");
    auto r = parse(cli, {"prog", "--help"});
    EXPECT_TRUE(r.flag("--help"));
}

TEST(Cli, FlagShort) {
    CliParser cli("prog");
    cli.flag("--verbose", "-v", "Verbose");
    auto r = parse(cli, {"prog", "-v"});
    EXPECT_TRUE(r.flag("--verbose"));
}

TEST(Cli, FlagAbsent) {
    CliParser cli("prog");
    cli.flag("--debug", "-d", "Debug");
    auto r = parse(cli, {"prog"});
    EXPECT_FALSE(r.flag("--debug"));
}

TEST(Cli, OptionLong) {
    CliParser cli("prog");
    cli.option("--config", "-c", "FILE", "Config file");
    auto r = parse(cli, {"prog", "--config", "run.yaml"});
    EXPECT_EQ(r.option("--config", ""), "run.yaml");
}

TEST(Cli, OptionShort) {
    CliParser cli("prog");
    cli.option("--output", "-o", "DIR", "Output dir");
    auto r = parse(cli, {"prog", "-o", "/tmp/out"});
    EXPECT_EQ(r.option("--output", ""), "/tmp/out");
}

TEST(Cli, OptionDefault) {
    CliParser cli("prog");
    cli.option("--config", "-c", "FILE", "Config file");
    auto r = parse(cli, {"prog"});
    EXPECT_EQ(r.option("--config", "default.yaml"), "default.yaml");
}

TEST(Cli, OptionMissingArgError) {
    CliParser cli("prog");
    cli.option("--config", "-c", "FILE", "Config file");
    auto r = parse(cli, {"prog", "--config"});   // no value follows
    EXPECT_TRUE(r.has_errors());
}

TEST(Cli, PositionalArgs) {
    CliParser cli("prog");
    auto r = parse(cli, {"prog", "file1.csv", "file2.csv"});
    ASSERT_EQ(r.positional().size(), 2u);
    EXPECT_EQ(r.positional()[0], "file1.csv");
    EXPECT_EQ(r.positional()[1], "file2.csv");
}

TEST(Cli, MixedFlagsOptionsPositional) {
    CliParser cli("prog");
    cli.flag("--html",    "-H", "HTML");
    cli.option("--output","-o", "DIR", "Out");
    auto r = parse(cli, {"prog", "--html", "--output", "reports/", "input.csv"});
    EXPECT_TRUE(r.flag("--html"));
    EXPECT_EQ(r.option("--output", ""), "reports/");
    ASSERT_EQ(r.positional().size(), 1u);
    EXPECT_EQ(r.positional()[0], "input.csv");
}

TEST(Cli, MultipleFlagsSet) {
    CliParser cli("prog");
    cli.flag("--html", "-H", "HTML");
    cli.flag("--csv",  "-C", "CSV");
    auto r = parse(cli, {"prog", "--html", "--csv"});
    EXPECT_TRUE(r.flag("--html"));
    EXPECT_TRUE(r.flag("--csv"));
}
