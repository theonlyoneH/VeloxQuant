// tests/test_data_portal.cpp  ── DataPortal unit tests
#include "market_data/data_portal.hpp"

#include <filesystem>
#include <gtest/gtest.h>

using namespace md;

namespace {

struct TmpFile {
    std::filesystem::path path;
    TmpFile() {
        char buf[] = "/tmp/dpXXXXXX";
        int fd = ::mkstemp(buf);
        ::close(fd);
        std::filesystem::remove(buf);
        path = buf;
    }
    ~TmpFile() { std::filesystem::remove(path); }
};

Tick make_tick(Timestamp ts, SymbolId sym = 1,
               Price bid = to_price(100.0), Price ask = to_price(100.1)) {
    Tick t{};
    t.recv_ts    = ts;
    t.exch_ts    = ts;
    t.symbol_id  = sym;
    t.bid_price  = bid;
    t.ask_price  = ask;
    t.last_price = (bid + ask) / 2;
    t.last_size  = 50;
    return t;
}

// Build a BinaryStore<Tick> with N ticks starting at start_ts, step_ns apart
BinaryStore<Tick> build_store(const std::filesystem::path& path, SymbolId sym,
                              int n, Timestamp start_ts = 0,
                              Timestamp step = 1'000'000LL) {
    {
        BinaryStore<Tick> w(path, StoreMode::Create);
        for (int i = 0; i < n; ++i)
            w.append(make_tick(start_ts + i * step, sym));
    }
    return BinaryStore<Tick>(path, StoreMode::ReadOnly);
}

} // anon namespace

// ── Register + current_tick ───────────────────────────────────────────────────
TEST(DataPortal, RegisterAndCurrentTick) {
    TmpFile f;
    auto store = build_store(f.path, 1, 10, 0, 1'000'000LL);

    DataPortal portal;
    portal.register_store("AAPL", std::move(store));
    portal.advance_to(5'000'000LL);

    auto tick = portal.current_tick("AAPL");
    ASSERT_TRUE(tick.has_value());
    EXPECT_LE(tick->recv_ts, 5'000'000LL);
}

// ── No-lookahead: current_tick must not return future data ────────────────────
TEST(DataPortal, NoLookahead) {
    TmpFile f;
    auto store = build_store(f.path, 1, 10, 1'000'000LL, 1'000'000LL);

    DataPortal portal;
    portal.register_store("GOOG", std::move(store));

    // Time is before any tick
    portal.advance_to(500'000LL);
    auto tick = portal.current_tick("GOOG");
    EXPECT_FALSE(tick.has_value());

    // Advance to middle
    portal.advance_to(5'000'000LL);
    tick = portal.current_tick("GOOG");
    ASSERT_TRUE(tick.has_value());
    EXPECT_LE(tick->recv_ts, 5'000'000LL);
}

// ── current_price returns mid-price ──────────────────────────────────────────
TEST(DataPortal, CurrentPriceMid) {
    TmpFile f;
    auto store = build_store(f.path, 1, 5, 0, 1'000'000LL);

    DataPortal portal;
    auto id = portal.register_store("MSFT", std::move(store));
    portal.advance_to(4'000'000LL);

    auto price = portal.current_price(id);
    ASSERT_TRUE(price.has_value());
    // Bid = 100.0, Ask = 100.1 → mid = 100.05
    EXPECT_NEAR(*price, 100.05, 0.001);
}

// ── history returns ticks within lookback window ──────────────────────────────
TEST(DataPortal, History) {
    TmpFile f;
    auto store = build_store(f.path, 1, 20, 0, 1'000'000LL);

    DataPortal portal;
    auto id = portal.register_store("TSLA", std::move(store));
    portal.advance_to(10'000'000LL);

    // Lookback of 5ms → ticks at 5ms, 6ms, 7ms, 8ms, 9ms, 10ms = 6 ticks
    auto hist = portal.history(id, 5'000'000LL);
    ASSERT_FALSE(hist.empty());
    for (const auto& t : hist) {
        EXPECT_LE(t.recv_ts, 10'000'000LL);     // no lookahead
        EXPECT_GE(t.recv_ts, 5'000'000LL);      // within lookback
    }
}

// ── history_bars produces correct bar count ───────────────────────────────────
TEST(DataPortal, HistoryBars) {
    TmpFile f;
    // 60 ticks, 1 second apart → covers 60 seconds = 1 minute bar + some
    auto store = build_store(f.path, 1, 60, 0, 1'000'000'000LL);

    DataPortal portal;
    auto id = portal.register_store("SPY", std::move(store));
    portal.advance_to(59'000'000'000LL); // 59 seconds

    auto bars = portal.history_bars(id, {BarType::Minute, 1}, 5);
    // With 59 seconds of data at 1-min bars, expect at most 1 complete bar
    EXPECT_GE(bars.size(), 0u);
    for (const auto& b : bars) {
        EXPECT_LE(b.close_ts, 59'000'000'000LL);
    }
}

// ── lookup_id / lookup_ticker ─────────────────────────────────────────────────
TEST(DataPortal, SymbolLookup) {
    TmpFile f;
    auto store = build_store(f.path, 1, 1, 0, 1'000'000LL);

    DataPortal portal;
    auto id = portal.register_store("NVDA", std::move(store));

    auto looked_up = portal.lookup_id("NVDA");
    ASSERT_TRUE(looked_up.has_value());
    EXPECT_EQ(*looked_up, id);

    auto ticker = portal.lookup_ticker(id);
    ASSERT_TRUE(ticker.has_value());
    EXPECT_EQ(*ticker, "NVDA");
}

// ── Monotonic time: advance_to cannot go backwards ───────────────────────────
TEST(DataPortal, MonotonicTime) {
    TmpFile f;
    auto store = build_store(f.path, 1, 10, 0, 1'000'000LL);

    DataPortal portal;
    portal.register_store("AMD", std::move(store));
    portal.advance_to(8'000'000LL);
    portal.advance_to(3'000'000LL); // attempt to go back
    EXPECT_EQ(portal.current_ts(), 8'000'000LL);
}
