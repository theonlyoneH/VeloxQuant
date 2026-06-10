// tests/test_binary_store.cpp  ── BinaryStore unit tests
#include "market_data/binary_store.hpp"

#include <filesystem>
#include <gtest/gtest.h>
#include <cstdio>

using namespace md;

namespace {

// Helper: create a temporary file path that is removed after use.
struct TmpFile {
    std::filesystem::path path;
    TmpFile() {
        char buf[] = "/tmp/bsXXXXXX";
        int fd = ::mkstemp(buf);
        ::close(fd);
        std::filesystem::remove(buf); // BinaryStore Create will recreate it
        path = buf;
    }
    ~TmpFile() { std::filesystem::remove(path); }
};

// Build a simple Tick with given recv_ts
Tick make_tick(Timestamp ts, SymbolId sym = 1,
               Price bid = to_price(100.0), Price ask = to_price(100.05)) {
    Tick t{};
    t.recv_ts    = ts;
    t.exch_ts    = ts;
    t.symbol_id  = sym;
    t.bid_price  = bid;
    t.ask_price  = ask;
    t.last_price = (bid + ask) / 2;
    t.bid_size   = 100;
    t.ask_size   = 100;
    t.last_size  = 10;
    t.seq_no     = static_cast<SequenceNo>(ts);
    return t;
}

} // anon namespace

// ── Create / append / reopen ──────────────────────────────────────────────────
TEST(BinaryStore, CreateAppendReopen) {
    TmpFile tmp;

    // Create and populate
    {
        BinaryStore<Tick> store(tmp.path, StoreMode::Create);
        ASSERT_TRUE(store.is_open());
        EXPECT_EQ(store.size(), 0u);

        for (int i = 0; i < 100; ++i) {
            store.append(make_tick(i * 1'000'000LL));
        }
        EXPECT_EQ(store.size(), 100u);
        store.flush();
    }

    // Reopen read-only
    {
        BinaryStore<Tick> store(tmp.path, StoreMode::ReadOnly);
        ASSERT_TRUE(store.is_open());
        EXPECT_EQ(store.size(), 100u);
        EXPECT_EQ(store[0].recv_ts, 0LL);
        EXPECT_EQ(store[99].recv_ts, 99 * 1'000'000LL);
    }
}

// ── lower_bound correctness ────────────────────────────────────────────────────
TEST(BinaryStore, LowerBound) {
    TmpFile tmp;
    {
        BinaryStore<Tick> store(tmp.path, StoreMode::Create);
        for (int i = 0; i < 10; ++i)
            store.append(make_tick(i * 1'000LL));
    }
    BinaryStore<Tick> store(tmp.path, StoreMode::ReadOnly);

    // Exact hit
    auto it = store.lower_bound(5'000LL);
    EXPECT_EQ(it->recv_ts, 5'000LL);

    // Before all records
    it = store.lower_bound(-1LL);
    EXPECT_EQ(it, store.begin());

    // After all records
    it = store.lower_bound(100'000LL);
    EXPECT_EQ(it, store.end());

    // Between records
    it = store.lower_bound(3'500LL);
    EXPECT_EQ(it->recv_ts, 4'000LL);
}

// ── last_at_or_before (no-lookahead) ─────────────────────────────────────────
TEST(BinaryStore, LastAtOrBefore) {
    TmpFile tmp;
    {
        BinaryStore<Tick> store(tmp.path, StoreMode::Create);
        for (int i = 1; i <= 10; ++i)
            store.append(make_tick(i * 1'000LL));
    }
    BinaryStore<Tick> store(tmp.path, StoreMode::ReadOnly);

    // Exactly on a record
    auto r = store.last_at_or_before(5'000LL);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->recv_ts, 5'000LL);

    // Between records
    r = store.last_at_or_before(5'500LL);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->recv_ts, 5'000LL);

    // Before all records → nullopt
    r = store.last_at_or_before(500LL);
    EXPECT_FALSE(r.has_value());

    // After all records
    r = store.last_at_or_before(99'999LL);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->recv_ts, 10'000LL);
}

// ── Move semantics ────────────────────────────────────────────────────────────
TEST(BinaryStore, MoveConstruct) {
    TmpFile tmp;
    {
        BinaryStore<Tick> s(tmp.path, StoreMode::Create);
        s.append(make_tick(1000LL));
    }
    BinaryStore<Tick> s1(tmp.path, StoreMode::ReadOnly);
    BinaryStore<Tick> s2(std::move(s1));
    EXPECT_FALSE(s1.is_open());
    EXPECT_TRUE(s2.is_open());
    EXPECT_EQ(s2.size(), 1u);
}

// ── Iterator range ─────────────────────────────────────────────────────────────
TEST(BinaryStore, IteratorRange) {
    TmpFile tmp;
    {
        BinaryStore<Tick> s(tmp.path, StoreMode::Create);
        for (int i = 0; i < 5; ++i) s.append(make_tick(i * 100LL));
    }
    BinaryStore<Tick> s(tmp.path, StoreMode::ReadOnly);
    std::size_t count = 0;
    Timestamp prev    = -1;
    for (const auto& t : s) {
        EXPECT_GT(t.recv_ts, prev);
        prev = t.recv_ts;
        ++count;
    }
    EXPECT_EQ(count, 5u);
}

// ── Read-only append throws ───────────────────────────────────────────────────
TEST(BinaryStore, ReadOnlyAppendThrows) {
    TmpFile tmp;
    {
        BinaryStore<Tick> s(tmp.path, StoreMode::Create);
        s.append(make_tick(1LL));
    }
    BinaryStore<Tick> s(tmp.path, StoreMode::ReadOnly);
    EXPECT_THROW(s.append(make_tick(2LL)), std::logic_error);
}
