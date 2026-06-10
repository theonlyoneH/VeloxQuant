#pragma once
// networking/tcp_feed.hpp  ── Async TCP market data feed (epoll, edge-triggered)
//
// Design:
//  • AsyncTcpFeed wraps a non-blocking TCP socket managed by epoll(7).
//  • It supports two modes:
//      Server: listens for connections, publishes encoded market data to all
//              connected clients.
//      Client: connects to a server, decodes incoming data and fires callbacks.
//  • All I/O is driven by a single std::jthread event loop.
//  • A lock-free SPSC ring buffer decouples the publisher from the network.
//  • Reconnect logic (exponential backoff) is built into client mode.
//  • The BinaryParser is integrated per-connection.

#include "networking/binary_protocol.hpp"
#include "market_data/types.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Linux networking
#include <sys/epoll.h>
#include <netinet/in.h>

namespace net {

// ── Ring buffer (lock-free SPSC) ─────────────────────────────────────────────
/// Power-of-two sized byte ring buffer.  One producer, one consumer, no mutex.
class SpscRingBuffer {
public:
    explicit SpscRingBuffer(std::size_t capacity);

    /// Try to write `data` into the ring.  Returns bytes written (0 if full).
    std::size_t try_write(std::span<const std::byte> data) noexcept;

    /// Try to read up to `out.size()` bytes.  Returns bytes read.
    std::size_t try_read(std::span<std::byte> out) noexcept;

    [[nodiscard]] std::size_t available_read()  const noexcept;
    [[nodiscard]] std::size_t available_write() const noexcept;
    [[nodiscard]] bool        empty()           const noexcept;

private:
    std::vector<std::byte>  data_;
    std::size_t             mask_;
    std::atomic<std::size_t> head_{0}; ///< Producer writes here
    std::atomic<std::size_t> tail_{0}; ///< Consumer reads here
};

// ── FeedMode ─────────────────────────────────────────────────────────────────
enum class FeedMode { Server, Client };

// ── Connection info ───────────────────────────────────────────────────────────
struct ConnectionInfo {
    int         fd;
    std::string remote_addr;
    uint16_t    remote_port;
};

// ── Callbacks ─────────────────────────────────────────────────────────────────
using ConnectCallback    = std::function<void(const ConnectionInfo&)>;
using DisconnectCallback = std::function<void(const ConnectionInfo&)>;
using TickRecvCallback   = std::function<void(const md::Tick&)>;
using BarRecvCallback    = std::function<void(const md::Bar&)>;

// ── AsyncTcpFeed ─────────────────────────────────────────────────────────────
class AsyncTcpFeed {
public:
    struct Config {
        FeedMode    mode;
        std::string host;          ///< Client: server host. Server: bind address (e.g. "0.0.0.0")
        uint16_t    port;
        std::size_t ring_capacity  {1 << 20};  ///< 1 MiB send ring
        int         backlog        {16};        ///< Server: listen backlog
        int         max_reconnect_attempts{-1}; ///< Client: -1 = infinite
        std::chrono::milliseconds reconnect_base_delay{100};
    };

    explicit AsyncTcpFeed(Config cfg);
    ~AsyncTcpFeed() noexcept;

    // Non-copyable, movable
    AsyncTcpFeed(const AsyncTcpFeed&)            = delete;
    AsyncTcpFeed& operator=(const AsyncTcpFeed&) = delete;
    AsyncTcpFeed(AsyncTcpFeed&&)                 = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    void start(); ///< Launch event loop jthread
    void stop();  ///< Graceful shutdown; blocks until thread exits

    // ── Publish (Server mode) ─────────────────────────────────────────────────
    /// Enqueue a tick for broadcast to all connected clients.
    /// Thread-safe: can be called from any thread.
    bool publish(const md::Tick& tick);
    bool publish(const md::Bar&  bar);
    bool publish_heartbeat();

    // ── Subscribe (Client mode) ───────────────────────────────────────────────
    void on_tick(TickRecvCallback cb)         { tick_cb_    = std::move(cb); }
    void on_bar(BarRecvCallback cb)           { bar_cb_     = std::move(cb); }
    void on_connect(ConnectCallback cb)       { conn_cb_    = std::move(cb); }
    void on_disconnect(DisconnectCallback cb) { disconn_cb_ = std::move(cb); }

    // ── Diagnostics ───────────────────────────────────────────────────────────
    [[nodiscard]] bool        is_running()       const noexcept;
    [[nodiscard]] std::size_t connected_clients() const noexcept;
    [[nodiscard]] uint64_t    ticks_sent()        const noexcept { return ticks_sent_.load(); }
    [[nodiscard]] uint64_t    ticks_received()    const noexcept { return ticks_recv_.load(); }

private:
    Config                    cfg_;
    int                       epoll_fd_{-1};
    int                       listen_fd_{-1}; ///< Server mode
    int                       conn_fd_{-1};   ///< Client mode

    std::jthread              io_thread_;
    std::atomic<bool>         running_{false};

    // ── Per-client state (server mode) ────────────────────────────────────────
    struct ClientState {
        int               fd;
        std::string       addr;
        uint16_t          port;
        SpscRingBuffer    send_ring;
        BinaryParser      parser;

        ClientState(int fd_, std::string addr_, uint16_t port_,
                    std::size_t ring_cap, net::BinaryParser::MessageCallback cb)
            : fd(fd_), addr(std::move(addr_)), port(port_)
            , send_ring(ring_cap), parser(std::move(cb)) {}
    };
    std::unordered_map<int, std::unique_ptr<ClientState>> clients_;

    // ── Callbacks ─────────────────────────────────────────────────────────────
    TickRecvCallback    tick_cb_;
    BarRecvCallback     bar_cb_;
    ConnectCallback     conn_cb_;
    DisconnectCallback  disconn_cb_;

    // ── Stats ─────────────────────────────────────────────────────────────────
    std::atomic<uint64_t> ticks_sent_{0};
    std::atomic<uint64_t> ticks_recv_{0};

    // ── Internal helpers ─────────────────────────────────────────────────────
    void  run_server(std::stop_token st);
    void  run_client(std::stop_token st);
    void  setup_epoll_server();
    void  setup_listen_socket();
    void  accept_client();
    void  remove_client(int fd);
    void  drain_send_rings();
    void  read_client(int fd);
    int   connect_to_server();
    void  set_nonblocking(int fd);
    void  set_tcp_nodelay(int fd);
    bool  enqueue_to_all(std::span<const std::byte> frame);

    BinaryParser::MessageCallback make_parser_cb();
};

} // namespace net
