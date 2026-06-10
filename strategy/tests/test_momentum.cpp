// tests/test_momentum.cpp  ── MomentumStrategy unit tests
#include "strategy/momentum.hpp"
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

} // anon

// ── Warmup: needs lookback+1 prices ──────────────────────────────────────────
TEST(Momentum, WarmupPeriod) {
    MomentumStrategy s({.symbol_id = 1, .lookback = 5, .threshold = 0.02});
    std::vector<Signal> sigs;
    s.on_signal([&](const Signal& sg){ sigs.push_back(sg); });

    // Feed lookback bars (not enough yet – needs lookback+1)
    for (int i = 0; i < 5; ++i)
        s.on_bar(make_bar(1, 100.0));
    EXPECT_FALSE(s.is_warmed_up());
    EXPECT_TRUE(sigs.empty());

    s.on_bar(make_bar(1, 100.0));
    EXPECT_TRUE(s.is_warmed_up());
}

// ── Flat when ROC below threshold ─────────────────────────────────────────────
TEST(Momentum, FlatBelowThreshold) {
    // threshold = 0.05 (5%)
    MomentumStrategy s({.symbol_id = 1, .lookback = 3, .threshold = 0.05});
    std::vector<Signal> sigs;
    s.on_signal([&](const Signal& sg){ sigs.push_back(sg); });

    // Price rises only 1% → below 5% threshold → Flat
    s.on_bar(make_bar(1, 100.0));
    s.on_bar(make_bar(1, 100.0));
    s.on_bar(make_bar(1, 100.0));
    s.on_bar(make_bar(1, 101.0)); // ROC = 1%

    for (const auto& sg : sigs)
        EXPECT_EQ(sg.type, SignalType::Flat);
}

// ── Long when ROC exceeds positive threshold ──────────────────────────────────
TEST(Momentum, LongSignalPositiveROC) {
    MomentumStrategy s({.symbol_id = 1, .lookback = 3, .threshold = 0.02});
    std::vector<Signal> sigs;
    s.on_signal([&](const Signal& sg){ sigs.push_back(sg); });

    // 3 bars at 100, then bar at 110 → ROC = (110-100)/100 = 10% >> 2%
    s.on_bar(make_bar(1, 100.0));
    s.on_bar(make_bar(1, 100.0));
    s.on_bar(make_bar(1, 100.0));
    s.on_bar(make_bar(1, 110.0));

    bool found_long = false;
    for (const auto& sg : sigs)
        if (sg.type == SignalType::Long) { found_long = true; break; }
    EXPECT_TRUE(found_long);
    EXPECT_NEAR(s.raw_roc(), 0.10, 1e-6);
}

// ── Short when ROC below negative threshold ────────────────────────────────────
TEST(Momentum, ShortSignalNegativeROC) {
    MomentumStrategy s({.symbol_id = 1, .lookback = 3, .threshold = 0.02});
    std::vector<Signal> sigs;
    s.on_signal([&](const Signal& sg){ sigs.push_back(sg); });

    s.on_bar(make_bar(1, 100.0));
    s.on_bar(make_bar(1, 100.0));
    s.on_bar(make_bar(1, 100.0));
    s.on_bar(make_bar(1, 80.0));  // ROC = -20%

    bool found_short = false;
    for (const auto& sg : sigs)
        if (sg.type == SignalType::Short) { found_short = true; break; }
    EXPECT_TRUE(found_short);
    EXPECT_NEAR(s.raw_roc(), -0.20, 1e-6);
}

// ── EMA smoothing damps sharp moves ───────────────────────────────────────────
TEST(Momentum, EMASmoothing) {
    // alpha = 0.5 → aggressive smoothing
    MomentumStrategy s({.symbol_id = 1, .lookback = 2, .threshold = 0.01,
                        .ema_alpha = 0.5});

    s.on_bar(make_bar(1, 100.0));
    s.on_bar(make_bar(1, 100.0));
    s.on_bar(make_bar(1, 120.0));  // raw_roc = 0.20

    // smoothed_roc = alpha * raw + (1-alpha) * prev = 0.5 * 0.20 + 0.5 * 0 = 0.10
    // (first EMA initialised = raw_roc)
    EXPECT_NEAR(s.raw_roc(), 0.20, 1e-6);
    // After first computation, smoothed == raw (initialised):
    EXPECT_NEAR(s.smoothed_roc(), 0.20, 1e-6);

    // Feed another bar
    s.on_bar(make_bar(1, 110.0)); // raw_roc = (110-100)/100 = 0.10
    // smoothed = 0.5*0.10 + 0.5*0.20 = 0.15
    EXPECT_NEAR(s.raw_roc(), 0.10, 1e-6);
    EXPECT_NEAR(s.smoothed_roc(), 0.15, 1e-6);
}

// ── Signal strength ∈ [0, 1] ─────────────────────────────────────────────────
TEST(Momentum, StrengthBounded) {
    MomentumStrategy s({.symbol_id = 1, .lookback = 2, .threshold = 0.02});
    std::vector<Signal> sigs;
    s.on_signal([&](const Signal& sg){ sigs.push_back(sg); });

    s.on_bar(make_bar(1, 100.0));
    s.on_bar(make_bar(1, 100.0));
    s.on_bar(make_bar(1, 200.0));  // massive ROC → strength should cap at 1.0

    for (const auto& sg : sigs) {
        EXPECT_GE(sg.strength, 0.0);
        EXPECT_LE(sg.strength, 1.0);
    }
}

// ── reset ─────────────────────────────────────────────────────────────────────
TEST(Momentum, ResetClearsState) {
    MomentumStrategy s({.symbol_id = 1, .lookback = 3, .threshold = 0.02});
    s.on_bar(make_bar(1, 100.0));
    s.on_bar(make_bar(1, 105.0));
    s.on_bar(make_bar(1, 110.0));
    s.on_bar(make_bar(1, 120.0));
    EXPECT_TRUE(s.is_warmed_up());

    s.reset();
    EXPECT_FALSE(s.is_warmed_up());
    EXPECT_NEAR(s.raw_roc(), 0.0, 1e-9);
}

// ── Symbol filter ─────────────────────────────────────────────────────────────
TEST(Momentum, IgnoresWrongSymbol) {
    MomentumStrategy s({.symbol_id = 1, .lookback = 3, .threshold = 0.01});
    std::vector<Signal> sigs;
    s.on_signal([&](const Signal& sg){ sigs.push_back(sg); });

    for (int i = 0; i < 10; ++i)
        s.on_bar(make_bar(2, 100.0 + i * 5.0));  // symbol 2

    EXPECT_TRUE(sigs.empty());
    EXPECT_FALSE(s.is_warmed_up());
}

// ── Exact ROC computation ─────────────────────────────────────────────────────
TEST(Momentum, ExactROC) {
    // lookback = 4: uses price 4 bars ago as the base
    MomentumStrategy s({.symbol_id = 1, .lookback = 4, .threshold = 0.001});

    // Bars: 100, 100, 100, 100, then 105
    // price 4 bars ago when 5th bar arrives = 100
    // ROC = (105 - 100) / 100 = 0.05
    s.on_bar(make_bar(1, 100.0));
    s.on_bar(make_bar(1, 100.0));
    s.on_bar(make_bar(1, 100.0));
    s.on_bar(make_bar(1, 100.0));
    s.on_bar(make_bar(1, 105.0));

    EXPECT_NEAR(s.raw_roc(), 0.05, 1e-7);
}
