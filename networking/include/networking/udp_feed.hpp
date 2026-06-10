#pragma once
// networking/udp_feed.hpp  ── Async UDP multicast market data feed
//
// Design:
//  • AsyncUdpFeed sends/receives market data over UDP multicast.
//  • Publisher: joins the multicast group and sends encoded frames.
//  • Subscriber: joins the multicast group, receives and decodes frames.
//  • Each frame carries a per-symbol sequence number for gap detection.
//  • Out-of-order frames are buffered in a small fixed-size reorder window.
//  • Gaps trigger an optional retransmit request via a TCP back-channel.
//  • The receive loop runs in a std::jthread with a configurable SO_RCVBUF.

#include "networking/binary_protocol.hpp"
#include "market_data/types.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

// Linux networking
#include <netinet/in.h>
#include <sys/socket.h>

namespace net {

// ── UdpFeedMode ──────────────────────────────────────────────────────────────
enum class UdpFeedMode { Publisher, Subscriber };

// ── Gap event ─────────────────────────────────────────────────────────────────
struct GapEvent {
    md::SymbolId  symbol_id;
    uint64_t      expected_seq;
    uint64_t      received_seq;
};

// ── AsyncUdpFeed ──────────────────────────────────────────────────────────────
class AsyncUdpFeed {
public:
    struct Config {
        UdpFeedMode  mode;
        std::string  multicast_group;   ///< e.g. "239.255.0.1"
        uint16_t     port;
        std::string  interface_addr;    ///< e.g. "0.0.0.0" or specific NIC
        int          ttl            {1};
        int          recv_buf_size  {1 << 20}; ///< 1 MiB SO_RCVBUF
        std::size_t  reorder_window {16};      ///< Out-of-order frame buffer depth
        bool         loop_back      {true};    ///< IP_MULTICAST_LOOP (for testing)
    };

    explicit AsyncUdpFeed(Config cfg);
    ~AsyncUdpFeed() noexcept;

    // Non-copyable
    AsyncUdpFeed(const AsyncUdpFeed&)            = delete;
    AsyncUdpFeed& operator=(const AsyncUdpFeed&) = delete;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    void start();
    void stop();

    // ── Publisher interface ───────────────────────────────────────────────────
    bool publish(const md::Tick& tick);
    bool publish(const md::Bar&  bar);
    bool publish_heartbeat();

    // ── Subscriber callbacks ──────────────────────────────────────────────────
    using TickCallback = std::function<void(const md::Tick&)>;
    using BarCallback  = std::function<void(const md::Bar&)>;
    using GapCallback  = std::function<void(const GapEvent&)>;

    void on_tick(TickCallback cb)  { tick_cb_ = std::move(cb); }
    void on_bar(BarCallback cb)    { bar_cb_  = std::move(cb); }
    void on_gap(GapCallback cb)    { gap_cb_  = std::move(cb); }

    // ── Diagnostics ───────────────────────────────────────────────────────────
    [[nodiscard]] uint64_t packets_sent()     const noexcept { return pkts_sent_.load();  }
    [[nodiscard]] uint64_t packets_received() const noexcept { return pkts_recv_.load();  }
    [[nodiscard]] uint64_t gaps_detected()    const noexcept { return gaps_detected_.load(); }
    [[nodiscard]] bool     is_running()       const noexcept { return running_.load();    }

private:
    Config    cfg_;
    int       sock_fd_{-1};
    sockaddr_in mcast_addr_{};

    std::jthread       recv_thread_;
    std::atomic<bool>  running_{false};

    // ── Per-symbol sequence tracking ─────────────────────────────────────────
    struct SeqState {
        uint64_t expected{0};
        // Small reorder buffer: seq_no → raw frame bytes
        std::map<uint64_t, std::vector<std::byte>> pending;
    };
    std::unordered_map<md::SymbolId, SeqState> seq_map_;

    // ── Callbacks ─────────────────────────────────────────────────────────────
    TickCallback tick_cb_;
    BarCallback  bar_cb_;
    GapCallback  gap_cb_;

    // ── Stats ─────────────────────────────────────────────────────────────────
    std::atomic<uint64_t> pkts_sent_    {0};
    std::atomic<uint64_t> pkts_recv_    {0};
    std::atomic<uint64_t> gaps_detected_{0};

    // ── Helpers ──────────────────────────────────────────────────────────────
    void setup_publisher_socket();
    void setup_subscriber_socket();
    void recv_loop(std::stop_token st);
    void handle_datagram(std::span<const std::byte> dgram);
    void dispatch_decoded(const ParseResult& result);
    bool send_frame(std::span<const std::byte> frame);

    // ── Sequence number embedded in UDP payload header ─────────────────────
    // We prepend a mini-header before the binary protocol frame:
    //   4 bytes symbol_id + 8 bytes seq_no  = 12 bytes
    static constexpr std::size_t kUdpPrefixSize = 12;

    uint64_t next_seq_{0}; ///< Publisher-side sequence counter
    static constexpr std::size_t kMaxDgramSize = 65507;
};

} // namespace net
