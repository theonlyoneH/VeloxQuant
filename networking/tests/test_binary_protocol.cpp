// tests/test_binary_protocol.cpp  ── BinaryParser + encoders unit tests
#include "networking/binary_protocol.hpp"

#include <gtest/gtest.h>
#include <cstring>
#include <vector>

using namespace net;
using namespace md;

// ── Round-trip: Tick encode → decode ─────────────────────────────────────────
TEST(BinaryProtocol, TickRoundTrip) {
    Tick t{};
    t.recv_ts   = 123'456'789LL;
    t.exch_ts   = 123'456'700LL;
    t.symbol_id = 42;
    t.bid_price = to_price(100.50);
    t.ask_price = to_price(100.55);
    t.last_price= to_price(100.52);
    t.bid_size  = 500;
    t.ask_size  = 300;
    t.last_size = 100;
    t.seq_no    = 7;

    std::array<std::byte, kMaxFrameSize> buf{};
    const std::size_t n = encode(t, buf);
    ASSERT_GT(n, 0u);
    EXPECT_EQ(n, kFrameOverhead + sizeof(Tick));

    std::optional<Tick> received;
    BinaryParser parser([&](ParseResult r) {
        ASSERT_TRUE(r.ok);
        ASSERT_TRUE(std::holds_alternative<Tick>(r.message));
        received = std::get<Tick>(r.message);
    });
    parser.push({buf.data(), n});

    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(received->recv_ts,   t.recv_ts);
    EXPECT_EQ(received->symbol_id, t.symbol_id);
    EXPECT_EQ(received->bid_price, t.bid_price);
    EXPECT_EQ(received->ask_price, t.ask_price);
    EXPECT_EQ(received->seq_no,    t.seq_no);
}

// ── Round-trip: Bar encode → decode ──────────────────────────────────────────
TEST(BinaryProtocol, BarRoundTrip) {
    Bar b{};
    b.open_ts   = 0;
    b.close_ts  = 60'000'000'000LL;
    b.symbol_id = 5;
    b.open      = to_price(200.0);
    b.high      = to_price(210.0);
    b.low       = to_price(198.0);
    b.close     = to_price(205.0);
    b.volume    = 1'000'000;
    b.trade_count = 5000;

    std::array<std::byte, kMaxFrameSize> buf{};
    const std::size_t n = encode(b, buf);
    ASSERT_GT(n, 0u);

    std::optional<Bar> received;
    BinaryParser parser([&](ParseResult r) {
        ASSERT_TRUE(r.ok);
        ASSERT_TRUE(std::holds_alternative<Bar>(r.message));
        received = std::get<Bar>(r.message);
    });
    parser.push({buf.data(), n});

    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(received->high,  b.high);
    EXPECT_EQ(received->low,   b.low);
    EXPECT_EQ(received->close, b.close);
}

// ── Heartbeat round-trip ──────────────────────────────────────────────────────
TEST(BinaryProtocol, HeartbeatRoundTrip) {
    HeartbeatMsg hb{.server_ts = 999'888'777LL};
    std::array<std::byte, kMaxFrameSize> buf{};
    const std::size_t n = encode(hb, buf);
    ASSERT_GT(n, 0u);

    bool received = false;
    BinaryParser parser([&](ParseResult r) {
        ASSERT_TRUE(r.ok);
        EXPECT_TRUE(std::holds_alternative<HeartbeatMsg>(r.message));
        received = true;
    });
    parser.push({buf.data(), n});
    EXPECT_TRUE(received);
}

// ── CRC corruption detected ───────────────────────────────────────────────────
TEST(BinaryProtocol, CRCCorruptionDetected) {
    Tick t{};
    t.recv_ts   = 1LL;
    t.symbol_id = 1;

    std::array<std::byte, kMaxFrameSize> buf{};
    const std::size_t n = encode(t, buf);
    ASSERT_GT(n, 0u);

    // Flip a byte in the payload (byte 10, inside the Tick payload)
    buf[10] ^= std::byte{0xFF};

    int bad = 0;
    BinaryParser parser([&](ParseResult r) {
        if (!r.ok) ++bad;
    });
    parser.push({buf.data(), n});
    EXPECT_GT(bad, 0);
}

// ── Streaming: multiple frames concatenated ───────────────────────────────────
TEST(BinaryProtocol, StreamingMultipleFrames) {
    std::vector<std::byte> stream;
    constexpr int N = 10;

    for (int i = 0; i < N; ++i) {
        Tick t{};
        t.recv_ts   = static_cast<Timestamp>(i);
        t.symbol_id = 1;
        std::array<std::byte, kMaxFrameSize> buf{};
        const std::size_t n = encode(t, buf);
        stream.insert(stream.end(), buf.data(), buf.data() + n);
    }

    int count = 0;
    BinaryParser parser([&](ParseResult r) {
        EXPECT_TRUE(r.ok);
        ++count;
    });
    // Feed one byte at a time to stress the state machine
    for (auto b : stream) {
        parser.push({&b, 1});
    }
    EXPECT_EQ(count, N);
}

// ── Streaming: garbage + valid frame ─────────────────────────────────────────
TEST(BinaryProtocol, GarbageThenValidFrame) {
    // Prepend random garbage before a valid frame
    std::vector<std::byte> stream;
    const std::array<std::byte, 20> garbage{
        std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF},
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
        std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x44},
        std::byte{0x55}, std::byte{0x66}, std::byte{0x77}, std::byte{0x88},
        std::byte{0x99}, std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC},
    };
    stream.insert(stream.end(), garbage.begin(), garbage.end());

    Tick t{};
    t.recv_ts   = 42LL;
    t.symbol_id = 7;
    std::array<std::byte, kMaxFrameSize> buf{};
    const std::size_t n = encode(t, buf);
    stream.insert(stream.end(), buf.data(), buf.data() + n);

    std::optional<Tick> received;
    BinaryParser parser([&](ParseResult r) {
        if (r.ok && std::holds_alternative<Tick>(r.message))
            received = std::get<Tick>(r.message);
    });
    parser.push(stream);

    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(received->recv_ts, 42LL);
}

// ── Subscribe request round-trip ──────────────────────────────────────────────
TEST(BinaryProtocol, SubscribeRoundTrip) {
    SubscribeRequest req{.symbol_id = 123, .flags = 0x03};
    std::array<std::byte, kMaxFrameSize> buf{};
    const std::size_t n = encode(req, buf);
    ASSERT_GT(n, 0u);

    std::optional<SubscribeRequest> received;
    BinaryParser parser([&](ParseResult r) {
        if (r.ok && std::holds_alternative<SubscribeRequest>(r.message))
            received = std::get<SubscribeRequest>(r.message);
    });
    parser.push({buf.data(), n});

    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(received->symbol_id, 123u);
    EXPECT_EQ(received->flags,     0x03u);
}

// ── CRC32 determinism ─────────────────────────────────────────────────────────
TEST(BinaryProtocol, CRC32Deterministic) {
    const std::array<std::byte, 4> data{
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}};
    const uint32_t a = crc32(data);
    const uint32_t b = crc32(data);
    EXPECT_EQ(a, b);
    EXPECT_NE(a, 0u);
}

// ── Decoder counters ──────────────────────────────────────────────────────────
TEST(BinaryProtocol, DecoderCounters) {
    BinaryParser parser([](ParseResult){});

    Tick t{};
    std::array<std::byte, kMaxFrameSize> buf{};
    const std::size_t n = encode(t, buf);

    parser.push({buf.data(), n});
    parser.push({buf.data(), n});
    EXPECT_EQ(parser.frames_decoded(), 2u);

    // Corrupt one frame
    auto corrupted = buf;
    corrupted[10] ^= std::byte{0xFF};
    parser.push({corrupted.data(), n});
    EXPECT_EQ(parser.frames_rejected(), 1u);
}
