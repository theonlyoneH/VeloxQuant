// tests/test_tcp_feed.cpp  ── AsyncTcpFeed integration tests
//
// Tests run a loopback server+client on 127.0.0.1 using ephemeral ports.
// The server is started first, then the client connects.

#include "networking/tcp_feed.hpp"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

// Linux: get a free port
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace net;
using namespace md;
using namespace std::chrono_literals;

namespace {

/// Allocate a free ephemeral port by binding a temporary socket.
uint16_t get_free_port() {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = 0; // Let OS choose
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    socklen_t len = sizeof(addr);
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    const uint16_t port = ntohs(addr.sin_port);
    ::close(fd);
    return port;
}

Tick make_tick(Timestamp ts = 1000, SymbolId sym = 1) {
    Tick t{};
    t.recv_ts   = ts;
    t.symbol_id = sym;
    t.bid_price = to_price(100.0);
    t.ask_price = to_price(100.1);
    t.last_price= to_price(100.05);
    t.bid_size  = 100; t.ask_size = 100; t.last_size = 10;
    return t;
}

} // anon namespace

// ── Server start / stop ───────────────────────────────────────────────────────
TEST(TcpFeed, ServerStartStop) {
    const uint16_t port = get_free_port();
    AsyncTcpFeed server({
        .mode = FeedMode::Server,
        .host = "127.0.0.1",
        .port = port,
    });
    server.start();
    std::this_thread::sleep_for(50ms);
    EXPECT_TRUE(server.is_running());
    server.stop();
    EXPECT_FALSE(server.is_running());
}

// ── Client connect + disconnect ───────────────────────────────────────────────
TEST(TcpFeed, ClientConnect) {
    const uint16_t port = get_free_port();

    AsyncTcpFeed server({
        .mode = FeedMode::Server,
        .host = "127.0.0.1",
        .port = port,
    });
    server.start();
    std::this_thread::sleep_for(30ms);

    std::atomic<bool> connected{false};
    AsyncTcpFeed client({
        .mode = FeedMode::Client,
        .host = "127.0.0.1",
        .port = port,
        .max_reconnect_attempts = 1,
    });
    client.on_connect([&](const ConnectionInfo&){ connected.store(true); });
    client.start();

    std::this_thread::sleep_for(200ms);
    EXPECT_TRUE(connected.load());

    client.stop();
    server.stop();
}

// ── Server publish → client receives tick ─────────────────────────────────────
TEST(TcpFeed, PublishReceive) {
    const uint16_t port = get_free_port();

    AsyncTcpFeed server({
        .mode = FeedMode::Server,
        .host = "127.0.0.1",
        .port = port,
    });
    server.start();
    std::this_thread::sleep_for(30ms);

    std::atomic<int> received{0};
    std::atomic<Timestamp> last_ts{0};

    AsyncTcpFeed client({
        .mode = FeedMode::Client,
        .host = "127.0.0.1",
        .port = port,
        .max_reconnect_attempts = 2,
    });
    client.on_tick([&](const Tick& t){
        received.fetch_add(1, std::memory_order_relaxed);
        last_ts.store(t.recv_ts, std::memory_order_relaxed);
    });
    client.start();

    // Wait for connection
    std::this_thread::sleep_for(100ms);

    // Publish ticks from server
    constexpr int N = 20;
    for (int i = 0; i < N; ++i) {
        server.publish(make_tick(i * 1000LL, 1));
        std::this_thread::sleep_for(2ms);
    }

    // Wait for all ticks to arrive
    std::this_thread::sleep_for(200ms);

    EXPECT_EQ(received.load(), N);
    EXPECT_EQ(last_ts.load(), (N - 1) * 1000LL);

    client.stop();
    server.stop();
}

// ── Heartbeat publish ─────────────────────────────────────────────────────────
TEST(TcpFeed, HeartbeatPublish) {
    const uint16_t port = get_free_port();

    AsyncTcpFeed server({
        .mode = FeedMode::Server,
        .host = "127.0.0.1",
        .port = port,
    });
    server.start();
    std::this_thread::sleep_for(30ms);

    EXPECT_TRUE(server.publish_heartbeat()); // no clients connected – still encodes OK

    server.stop();
}

// ── Tick counter increments ───────────────────────────────────────────────────
TEST(TcpFeed, TickCounter) {
    const uint16_t port = get_free_port();
    AsyncTcpFeed server({
        .mode = FeedMode::Server,
        .host = "127.0.0.1",
        .port = port,
    });
    server.start();
    std::this_thread::sleep_for(30ms);

    AsyncTcpFeed client({
        .mode = FeedMode::Client,
        .host = "127.0.0.1",
        .port = port,
        .max_reconnect_attempts = 1,
    });
    std::atomic<int> cnt{0};
    client.on_tick([&](const Tick&){ cnt.fetch_add(1); });
    client.start();

    std::this_thread::sleep_for(80ms);

    constexpr int N = 5;
    for (int i = 0; i < N; ++i) server.publish(make_tick(i));
    std::this_thread::sleep_for(200ms);

    EXPECT_EQ(server.ticks_sent(), static_cast<uint64_t>(N));

    client.stop();
    server.stop();
}
