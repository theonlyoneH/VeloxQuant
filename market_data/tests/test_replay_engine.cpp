// tests/test_replay_engine.cpp  ── HistoricalReplayEngine unit tests
#include "market_data/replay_engine.hpp"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace md;

namespace {

// Build a synthetic tick vector with timestamps 0, step, 2*step, ... n*step
std::vector<Tick> make_ticks(int n, Timestamp step = 1'000'000LL,
                             SymbolId sym = 1) {
    std::vector<Tick> v;
    v.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        Tick t{};
        t.recv_ts   = i * step;
        t.exch_ts   = t.recv_ts;
        t.symbol_id = sym;
        t.bid_price = to_price(100.0 + i * 0.01);
        t.ask_price = to_price(100.05 + i * 0.01);
        t.seq_no    = static_cast<SequenceNo>(i);
        v.push_back(t);
    }
    return v;
}

} // anon namespace

// ── step returns ticks in order ───────────────────────────────────────────────
TEST(ReplayEngine, StepInOrder) {
    auto ticks = make_ticks(10);
    HistoricalReplayEngine engine(
        std::span<const Tick>{ticks},
        {.start_ts = 0, .end_ts = 10'000'000LL, .speed_multiplier = 0.0, .loop = false});

    int count   = 0;
    Timestamp prev = -1;
    while (true) {
        auto t = engine.step();
        if (!t) break;
        EXPECT_GT(t->recv_ts, prev);
        prev = t->recv_ts;
        ++count;
    }
    EXPECT_EQ(count, 10);
}

// ── No lookahead: end_ts is enforced ─────────────────────────────────────────
TEST(ReplayEngine, NoLookahead) {
    auto ticks = make_ticks(20, 1'000'000LL);
    // Window covers only first 10 ticks [0, 10ms)
    HistoricalReplayEngine engine(
        std::span<const Tick>{ticks},
        {.start_ts = 0, .end_ts = 10'000'000LL, .speed_multiplier = 0.0, .loop = false});

    std::vector<Timestamp> seen;
    while (true) {
        auto t = engine.step();
        if (!t) break;
        seen.push_back(t->recv_ts);
    }
    ASSERT_EQ(seen.size(), 10u);
    for (auto ts : seen) {
        EXPECT_LT(ts, 10'000'000LL);
    }
}

// ── seek + step ───────────────────────────────────────────────────────────────
TEST(ReplayEngine, Seek) {
    auto ticks = make_ticks(20, 1'000'000LL);
    HistoricalReplayEngine engine(
        std::span<const Tick>{ticks},
        {.start_ts = 0, .end_ts = 20'000'000LL, .speed_multiplier = 0.0, .loop = false});

    engine.seek(10'000'000LL);
    auto t = engine.step();
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->recv_ts, 10'000'000LL);
}

// ── rewind ────────────────────────────────────────────────────────────────────
TEST(ReplayEngine, Rewind) {
    auto ticks = make_ticks(5, 1'000'000LL);
    HistoricalReplayEngine engine(
        std::span<const Tick>{ticks},
        {.start_ts = 0, .end_ts = 5'000'000LL, .speed_multiplier = 0.0, .loop = false});

    while (engine.step()) {}
    engine.rewind();
    auto t = engine.step();
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->recv_ts, 0LL);
}

// ── step_to ───────────────────────────────────────────────────────────────────
TEST(ReplayEngine, StepTo) {
    auto ticks = make_ticks(20, 1'000'000LL);
    HistoricalReplayEngine engine(
        std::span<const Tick>{ticks},
        {.start_ts = 0, .end_ts = 20'000'000LL, .speed_multiplier = 0.0, .loop = false});

    engine.step_to(5'000'000LL);
    // Current ts should be 5ms
    EXPECT_LE(engine.current_ts(), 5'000'000LL);
    // Next step should give tick > 5ms
    auto t = engine.step();
    ASSERT_TRUE(t.has_value());
    EXPECT_GT(t->recv_ts, 5'000'000LL);
}

// ── Subscription callbacks fired ──────────────────────────────────────────────
TEST(ReplayEngine, Subscription) {
    auto ticks = make_ticks(5, 1'000'000LL);
    HistoricalReplayEngine engine(
        std::span<const Tick>{ticks},
        {.start_ts = 0, .end_ts = 5'000'000LL, .speed_multiplier = 0.0, .loop = false});

    std::vector<Timestamp> received;
    auto h = engine.subscribe([&](const Tick& t){ received.push_back(t.recv_ts); });

    while (engine.step()) {}

    ASSERT_EQ(received.size(), 5u);
    engine.unsubscribe(h);
}

// ── start/stop background thread ─────────────────────────────────────────────
TEST(ReplayEngine, BackgroundThread) {
    auto ticks = make_ticks(50, 100'000LL); // 50 ticks at 100us intervals
    HistoricalReplayEngine engine(
        std::span<const Tick>{ticks},
        // speed_multiplier = 0 → max speed
        {.start_ts = 0, .end_ts = 50 * 100'000LL, .speed_multiplier = 0.0, .loop = false});

    std::atomic<int> count{0};
    engine.subscribe([&](const Tick&){ count.fetch_add(1, std::memory_order_relaxed); });

    engine.start();
    // Give ample time for max-speed replay
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    engine.stop();

    EXPECT_EQ(count.load(), 50);
}
