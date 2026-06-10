// reporting/tests/test_config.cpp
#include "reporting/config.hpp"
#include <gtest/gtest.h>

using rpt::Config;

TEST(Config, EmptyParsesOk) {
    Config c;
    EXPECT_TRUE(c.parse(""));
    EXPECT_TRUE(c.empty());
}

TEST(Config, TopLevelKey) {
    Config c;
    c.parse("name: hello\nvalue: 42\n");
    EXPECT_EQ(c.get_string("name", ""), "hello");
    EXPECT_NEAR(c.get_double("value", 0.0), 42.0, 1e-9);
}

TEST(Config, SectionedKey) {
    Config c;
    c.parse("strategy:\n  name: sma\n  fast_period: 10\n");
    EXPECT_EQ(c.get_string("strategy.name", ""), "sma");
    EXPECT_NEAR(c.get_double("strategy.fast_period", 0.0), 10.0, 1e-9);
}

TEST(Config, BoolTrueVariants) {
    Config c;
    c.parse("a: true\nb: yes\nc: 1\n");
    EXPECT_TRUE(c.get_bool("a", false));
    EXPECT_TRUE(c.get_bool("b", false));
    EXPECT_TRUE(c.get_bool("c", false));
}

TEST(Config, BoolFalseVariants) {
    Config c;
    c.parse("a: false\nb: no\nc: 0\n");
    EXPECT_FALSE(c.get_bool("a", true));
    EXPECT_FALSE(c.get_bool("b", true));
    EXPECT_FALSE(c.get_bool("c", true));
}

TEST(Config, IntParsing) {
    Config c;
    c.parse("count: 1000000\n");
    EXPECT_EQ(c.get_int("count", 0), 1'000'000LL);
}

TEST(Config, MissingKeyDefault) {
    Config c;
    c.parse("x: 5\n");
    EXPECT_EQ(c.get_string("y", "default"), "default");
    EXPECT_NEAR(c.get_double("y", 3.14), 3.14, 1e-9);
}

TEST(Config, HasKey) {
    Config c;
    c.parse("foo: bar\n");
    EXPECT_TRUE(c.has("foo"));
    EXPECT_FALSE(c.has("baz"));
}

TEST(Config, SetAndGet) {
    Config c;
    c.set("key", "value");
    EXPECT_EQ(c.get_string("key", ""), "value");
}

TEST(Config, CommentsStripped) {
    Config c;
    c.parse("x: 10  # inline comment\ny: 20\n");
    EXPECT_NEAR(c.get_double("x", 0), 10.0, 1e-9);
    EXPECT_NEAR(c.get_double("y", 0), 20.0, 1e-9);
}

TEST(Config, QuotedStringValues) {
    Config c;
    c.parse("path: \"/usr/local/lib\"\nlabel: 'my label'\n");
    EXPECT_EQ(c.get_string("path", ""), "/usr/local/lib");
    EXPECT_EQ(c.get_string("label", ""), "my label");
}

TEST(Config, DumpRoundtrip) {
    Config c;
    c.set("alpha", "0.05");
    c.set("name", "test");
    const auto dumped = c.dump();
    EXPECT_NE(dumped.find("alpha"), std::string::npos);
    EXPECT_NE(dumped.find("name"),  std::string::npos);
}
