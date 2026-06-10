// tests/test_sma.cpp  ── SMAStrategy unit tests
#include "strategy/sma.hpp"
#include <gtest/gtest.h>
#include <vector>

using namespace strat;
using namespace md;

namespace {

Bar make_bar(SymbolId sym, double close, Timestamp ts = 0) {
    Bar b{};
    b.symbol_id = sym;
    b.close     = to_price(close);
    b.open_ts   = ts;
    b.close_ts  = ts;
    return b;
}

} // anon

// ── Not warmed up before slow_period bars ─────────────────────────────────────
TEST(SMA, WarmupPeriod) {
    SMAStrategy s({.symbol_id = 1, .fast_period = 3, .slow_period = 5});
    std::vector<Signal> sigs;
    s.on_signal([&](const Signal& sg){ sigs.push_back(sg); });

    for (int i = 0; i < 4; ++i)       // 4 bars < slow=5
        s.on_bar(make_bar(1, 100.0 + i));

    EXPECT_FALSE(s.is_warmed_up());
    EXPECT_TRUE(sigs.empty());
}

// ── Flat while fast stays below slow ─────────────────────────────────────────
TEST(SMA, NoSignalWhileFlat) {
    SMAStrategy s({.symbol_id = 1, .fast_period = 3, .slow_period = 5});
    std::vector<Signal> sigs;
    s.on_signal([&](const Signal& sg){ sigs.push_back(sg); });

    // Constant price → both SMAs identical → no cross
    for (int i = 0; i < 10; ++i)
        s.on_bar(make_bar(1, 100.0));

    // All signals should be Flat
    for (const auto& sg : sigs)
        EXPECT_EQ(sg.type, SignalType::Flat);
}

// ── Golden cross → Long ───────────────────────────────────────────────────────
TEST(SMA, GoldenCross) {
    SMAStrategy s({.symbol_id = 1, .fast_period = 3, .slow_period = 5});
    std::vector<Signal> sigs;
    s.on_signal([&](const Signal& sg){ sigs.push_back(sg); });

    // Feed slow warmup with low prices
    for (int i = 0; i < 5; ++i)
        s.on_bar(make_bar(1, 100.0));
    EXPECT_TRUE(s.is_warmed_up());

    // Push fast SMA above slow by feeding high prices
    s.on_bar(make_bar(1, 110.0));
    s.on_bar(make_bar(1, 115.0));
    s.on_bar(make_bar(1, 120.0));

    bool found_long = false;
    for (const auto& sg : sigs)
        if (sg.type == SignalType::Long) { found_long = true; break; }
    EXPECT_TRUE(found_long);
}

// ── Death cross → Short ───────────────────────────────────────────────────────
TEST(SMA, DeathCross) {
    SMAStrategy s({.symbol_id = 1, .fast_period = 3, .slow_period = 5});
    std::vector<Signal> sigs;
    s.on_signal([&](const Signal& sg){ sigs.push_back(sg); });

    // Warm up with high prices
    for (int i = 0; i < 5; ++i)
        s.on_bar(make_bar(1, 120.0));

    // Push fast SMA below slow
    s.on_bar(make_bar(1, 80.0));
    s.on_bar(make_bar(1, 75.0));
    s.on_bar(make_bar(1, 70.0));

    bool found_short = false;
    for (const auto& sg : sigs)
        if (sg.type == SignalType::Short) { found_short = true; break; }
    EXPECT_TRUE(found_short);
}

// ── Symbol filter ─────────────────────────────────────────────────────────────
TEST(SMA, IgnoresWrongSymbol) {
    SMAStrategy s({.symbol_id = 1, .fast_period = 3, .slow_period = 5});
    std::vector<Signal> sigs;
    s.on_signal([&](const Signal& sg){ sigs.push_back(sg); });

    // Feed bars for symbol 2 – should be ignored
    for (int i = 0; i < 20; ++i)
        s.on_bar(make_bar(2, 100.0 + i));
    EXPECT_TRUE(sigs.empty());
}

// ── reset() clears state ──────────────────────────────────────────────────────
TEST(SMA, ResetClearsState) {
    SMAStrategy s({.symbol_id = 1, .fast_period = 3, .slow_period = 5});
    for (int i = 0; i < 10; ++i) s.on_bar(make_bar(1, 100.0 + i));
    EXPECT_TRUE(s.is_warmed_up());
    s.reset();
    EXPECT_FALSE(s.is_warmed_up());
    EXPECT_NEAR(s.fast_sma(), 0.0, 1e-9);
}

// ── fast_sma / slow_sma values ────────────────────────────────────────────────
TEST(SMA, SMAValues) {
    SMAStrategy s({.symbol_id = 1, .fast_period = 3, .slow_period = 5});
    const std::vector<double> prices = {1, 2, 3, 4, 5};
    for (double p : prices) s.on_bar(make_bar(1, p));

    // fast_sma(3) = mean(3,4,5) = 4.0
    // slow_sma(5) = mean(1,2,3,4,5) = 3.0
    EXPECT_NEAR(s.fast_sma(), 4.0, 1e-9);
    EXPECT_NEAR(s.slow_sma(), 3.0, 1e-9);
}

// ── strength is positive ──────────────────────────────────────────────────────
TEST(SMA, SignalStrengthPositive) {
    SMAStrategy s({.symbol_id = 1, .fast_period = 3, .slow_period = 5});
    std::vector<Signal> sigs;
    s.on_signal([&](const Signal& sg){ sigs.push_back(sg); });

    for (int i = 0; i < 5; ++i) s.on_bar(make_bar(1, 100.0));
    for (int i = 0; i < 5; ++i) s.on_bar(make_bar(1, 200.0));

    for (const auto& sg : sigs) {
        if (sg.type != SignalType::Flat) {
            EXPECT_GE(sg.strength, 0.0);
            EXPECT_LE(sg.strength, 1.0);
        }
    }
}
