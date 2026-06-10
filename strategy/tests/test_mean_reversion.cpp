// tests/test_mean_reversion.cpp  ── MeanReversionStrategy unit tests
#include "strategy/mean_reversion.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <vector>

using namespace strat;
using namespace md;

namespace {

Bar make_bar(SymbolId sym, double close, Timestamp ts = 0) {
    Bar b{};
    b.symbol_id = sym;
    b.close     = to_price(close);
    b.close_ts  = ts;
    return b;
}

void feed_n_bars(MeanReversionStrategy& s, SymbolId sym,
                 double price, int n) {
    for (int i = 0; i < n; ++i)
        s.on_bar(make_bar(sym, price));
}

} // anon

// ── Warmup period ─────────────────────────────────────────────────────────────
TEST(MeanReversion, WarmupPeriod) {
    MeanReversionStrategy s({.symbol_id = 1, .lookback = 5,
                              .entry_z = 2.0, .exit_z = 0.5});
    feed_n_bars(s, 1, 100.0, 4);
    EXPECT_FALSE(s.is_warmed_up());
    s.on_bar(make_bar(1, 100.0));
    EXPECT_TRUE(s.is_warmed_up());
}

// ── Rolling mean for constant series ─────────────────────────────────────────
TEST(MeanReversion, MeanConstant) {
    MeanReversionStrategy s({.symbol_id = 1, .lookback = 5});
    feed_n_bars(s, 1, 50.0, 5);
    EXPECT_NEAR(s.rolling_mean(), 50.0, 1e-6);
    EXPECT_NEAR(s.rolling_stddev(), 0.0, 1e-6);
}

// ── Rolling mean slides as window advances ────────────────────────────────────
TEST(MeanReversion, SlidingMean) {
    MeanReversionStrategy s({.symbol_id = 1, .lookback = 3});
    s.on_bar(make_bar(1, 10.0));
    s.on_bar(make_bar(1, 20.0));
    s.on_bar(make_bar(1, 30.0));
    EXPECT_NEAR(s.rolling_mean(), 20.0, 1e-6);

    // Push oldest (10) out
    s.on_bar(make_bar(1, 40.0));
    // Window = {20, 30, 40} → mean = 30
    EXPECT_NEAR(s.rolling_mean(), 30.0, 1e-6);
}

// ── Z-score → Short signal when price far above mean ─────────────────────────
TEST(MeanReversion, ShortSignalHighZ) {
    MeanReversionStrategy s({.symbol_id = 1, .lookback = 10,
                              .entry_z = 2.0, .exit_z = 0.5});
    std::vector<Signal> sigs;
    s.on_signal([&](const Signal& sg){ sigs.push_back(sg); });

    // Warm up near 100
    feed_n_bars(s, 1, 100.0, 10);

    // Feed a price far above mean (z >> 2)
    s.on_bar(make_bar(1, 200.0));

    bool found_short = false;
    for (const auto& sg : sigs)
        if (sg.type == SignalType::Short) { found_short = true; break; }
    EXPECT_TRUE(found_short);
}

// ── Z-score → Long signal when price far below mean ──────────────────────────
TEST(MeanReversion, LongSignalLowZ) {
    MeanReversionStrategy s({.symbol_id = 1, .lookback = 10,
                              .entry_z = 2.0, .exit_z = 0.5});
    std::vector<Signal> sigs;
    s.on_signal([&](const Signal& sg){ sigs.push_back(sg); });

    feed_n_bars(s, 1, 100.0, 10);
    s.on_bar(make_bar(1, 1.0));  // z << -2

    bool found_long = false;
    for (const auto& sg : sigs)
        if (sg.type == SignalType::Long) { found_long = true; break; }
    EXPECT_TRUE(found_long);
}

// ── Flat signal when z near zero ─────────────────────────────────────────────
TEST(MeanReversion, FlatSignalNearMean) {
    MeanReversionStrategy s({.symbol_id = 1, .lookback = 10,
                              .entry_z = 2.0, .exit_z = 0.5});
    std::vector<Signal> sigs;
    s.on_signal([&](const Signal& sg){ sigs.push_back(sg); });

    feed_n_bars(s, 1, 100.0, 10);
    s.on_bar(make_bar(1, 100.0));  // z ~ 0 → Flat

    for (const auto& sg : sigs)
        EXPECT_EQ(sg.type, SignalType::Flat);
}

// ── Z-score value is computable ───────────────────────────────────────────────
TEST(MeanReversion, ZScoreKnownValue) {
    // Prices: nine 10s then one 20
    // Window = {10, 10, 10, 10, 10, 10, 10, 10, 10, 20}
    // mean ≈ 11, var computed from sum-of-squares
    MeanReversionStrategy s({.symbol_id = 1, .lookback = 10});
    feed_n_bars(s, 1, 10.0, 9);
    s.on_bar(make_bar(1, 20.0));

    // Verify z is positive (price above mean)
    EXPECT_GT(s.z_score(), 0.0);
    EXPECT_GT(s.rolling_stddev(), 0.0);
}

// ── reset ─────────────────────────────────────────────────────────────────────
TEST(MeanReversion, ResetClearsState) {
    MeanReversionStrategy s({.symbol_id = 1, .lookback = 5});
    feed_n_bars(s, 1, 100.0, 5);
    EXPECT_TRUE(s.is_warmed_up());
    s.reset();
    EXPECT_FALSE(s.is_warmed_up());
    EXPECT_NEAR(s.rolling_mean(), 0.0, 1e-9);
}

// ── Signal strength in [0,1] ─────────────────────────────────────────────────
TEST(MeanReversion, StrengthBounded) {
    MeanReversionStrategy s({.symbol_id = 1, .lookback = 5,
                              .entry_z = 2.0, .exit_z = 0.5});
    std::vector<Signal> sigs;
    s.on_signal([&](const Signal& sg){ sigs.push_back(sg); });

    feed_n_bars(s, 1, 100.0, 5);
    s.on_bar(make_bar(1, 500.0));

    for (const auto& sg : sigs)
        if (sg.type != SignalType::Flat) {
            EXPECT_GE(sg.strength, 0.0);
            EXPECT_LE(sg.strength, 1.0);
        }
}
