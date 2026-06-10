// networking/udp_feed.cpp  ── Async UDP multicast feed implementation

#include "networking/udp_feed.hpp"

#include <cassert>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <system_error>

// Linux headers
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace net {

namespace {
void throw_errno(const char* ctx) {
    throw std::system_error(errno, std::generic_category(), ctx);
}

// Write little-endian uint32 / uint64 into a byte buffer
void write_u32le(std::byte* p, uint32_t v) noexcept {
    p[0] = static_cast<std::byte>(v & 0xFF);
    p[1] = static_cast<std::byte>((v >> 8) & 0xFF);
    p[2] = static_cast<std::byte>((v >> 16) & 0xFF);
    p[3] = static_cast<std::byte>((v >> 24) & 0xFF);
}
void write_u64le(std::byte* p, uint64_t v) noexcept {
    for (int i = 0; i < 8; ++i)
        p[i] = static_cast<std::byte>((v >> (8 * i)) & 0xFF);
}
uint32_t read_u32le(const std::byte* p) noexcept {
    return static_cast<uint32_t>(static_cast<uint8_t>(p[0]))
         | (static_cast<uint32_t>(static_cast<uint8_t>(p[1])) << 8)
         | (static_cast<uint32_t>(static_cast<uint8_t>(p[2])) << 16)
         | (static_cast<uint32_t>(static_cast<uint8_t>(p[3])) << 24);
}
uint64_t read_u64le(const std::byte* p) noexcept {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= static_cast<uint64_t>(static_cast<uint8_t>(p[i])) << (8 * i);
    return v;
}
} // anon namespace

// ─────────────────────────────────────────────────────────────────────────────
// Construction / destruction
// ─────────────────────────────────────────────────────────────────────────────
AsyncUdpFeed::AsyncUdpFeed(Config cfg) : cfg_(std::move(cfg)) {
    std::memset(&mcast_addr_, 0, sizeof(mcast_addr_));
    mcast_addr_.sin_family = AF_INET;
    mcast_addr_.sin_port   = htons(cfg_.port);
    ::inet_pton(AF_INET, cfg_.multicast_group.c_str(), &mcast_addr_.sin_addr);
}

AsyncUdpFeed::~AsyncUdpFeed() noexcept {
    stop();
    if (sock_fd_ >= 0) { ::close(sock_fd_); sock_fd_ = -1; }
}

// ─────────────────────────────────────────────────────────────────────────────
// Socket setup
// ─────────────────────────────────────────────────────────────────────────────
void AsyncUdpFeed::setup_publisher_socket() {
    sock_fd_ = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (sock_fd_ < 0) throw_errno("socket (UDP publisher)");

    // Set TTL
    const int ttl = cfg_.ttl;
    ::setsockopt(sock_fd_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    // Loopback (for testing)
    const int loop = cfg_.loop_back ? 1 : 0;
    ::setsockopt(sock_fd_, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

    // Bind to specific interface
    if (!cfg_.interface_addr.empty() && cfg_.interface_addr != "0.0.0.0") {
        in_addr iface{};
        ::inet_pton(AF_INET, cfg_.interface_addr.c_str(), &iface);
        ::setsockopt(sock_fd_, IPPROTO_IP, IP_MULTICAST_IF, &iface, sizeof(iface));
    }
}

void AsyncUdpFeed::setup_subscriber_socket() {
    sock_fd_ = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (sock_fd_ < 0) throw_errno("socket (UDP subscriber)");

    // Allow multiple processes to bind the same multicast port
    const int on = 1;
    ::setsockopt(sock_fd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    ::setsockopt(sock_fd_, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));

    // Set receive buffer
    const int rcvbuf = cfg_.recv_buf_size;
    ::setsockopt(sock_fd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    // Bind to multicast port
    sockaddr_in bind_addr{};
    bind_addr.sin_family      = AF_INET;
    bind_addr.sin_port        = htons(cfg_.port);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(sock_fd_, reinterpret_cast<sockaddr*>(&bind_addr),
               sizeof(bind_addr)) < 0)
        throw_errno("bind (UDP subscriber)");

    // Join multicast group
    ip_mreq mreq{};
    ::inet_pton(AF_INET, cfg_.multicast_group.c_str(), &mreq.imr_multiaddr);
    if (cfg_.interface_addr.empty() || cfg_.interface_addr == "0.0.0.0")
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    else
        ::inet_pton(AF_INET, cfg_.interface_addr.c_str(), &mreq.imr_interface);

    if (::setsockopt(sock_fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                     &mreq, sizeof(mreq)) < 0)
        throw_errno("IP_ADD_MEMBERSHIP");

    // Loopback
    const int loop = cfg_.loop_back ? 1 : 0;
    ::setsockopt(sock_fd_, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

    // Non-blocking
    int flags = ::fcntl(sock_fd_, F_GETFL, 0);
    ::fcntl(sock_fd_, F_SETFL, flags | O_NONBLOCK);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────
void AsyncUdpFeed::start() {
    if (running_.load())
        throw std::logic_error("AsyncUdpFeed already running");

    if (cfg_.mode == UdpFeedMode::Publisher) {
        setup_publisher_socket();
        running_.store(true);
        // Publisher has no dedicated thread – driven by publish() calls.
    } else {
        setup_subscriber_socket();
        running_.store(true);
        recv_thread_ = std::jthread([this](std::stop_token st){ recv_loop(st); });
    }
}

void AsyncUdpFeed::stop() {
    running_.store(false);
    if (recv_thread_.joinable()) {
        recv_thread_.request_stop();
        recv_thread_.join();
    }
    // Leave multicast group
    if (cfg_.mode == UdpFeedMode::Subscriber && sock_fd_ >= 0) {
        ip_mreq mreq{};
        ::inet_pton(AF_INET, cfg_.multicast_group.c_str(), &mreq.imr_multiaddr);
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        ::setsockopt(sock_fd_, IPPROTO_IP, IP_DROP_MEMBERSHIP,
                     &mreq, sizeof(mreq));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// UDP prefix helpers
// ─────────────────────────────────────────────────────────────────────────────
// UDP datagram layout:
//   [4 bytes symbol_id LE] [8 bytes seq_no LE] [N bytes binary frame]

bool AsyncUdpFeed::send_frame(std::span<const std::byte> frame) {
    const ssize_t n = ::sendto(sock_fd_, frame.data(), frame.size(), 0,
        reinterpret_cast<const sockaddr*>(&mcast_addr_), sizeof(mcast_addr_));
    return n == static_cast<ssize_t>(frame.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// Publish
// ─────────────────────────────────────────────────────────────────────────────
bool AsyncUdpFeed::publish(const md::Tick& tick) {
    // Encode the binary frame
    std::array<std::byte, kUdpPrefixSize + kMaxFrameSize> dgram{};
    write_u32le(dgram.data(), tick.symbol_id);
    write_u64le(dgram.data() + 4, next_seq_++);

    const std::size_t frame_len = encode(tick, {dgram.data() + kUdpPrefixSize,
                                                dgram.size() - kUdpPrefixSize});
    if (frame_len == 0) return false;

    pkts_sent_.fetch_add(1, std::memory_order_relaxed);
    return send_frame({dgram.data(), kUdpPrefixSize + frame_len});
}

bool AsyncUdpFeed::publish(const md::Bar& bar) {
    std::array<std::byte, kUdpPrefixSize + kMaxFrameSize> dgram{};
    write_u32le(dgram.data(), bar.symbol_id);
    write_u64le(dgram.data() + 4, next_seq_++);

    const std::size_t frame_len = encode(bar, {dgram.data() + kUdpPrefixSize,
                                               dgram.size() - kUdpPrefixSize});
    if (frame_len == 0) return false;

    pkts_sent_.fetch_add(1, std::memory_order_relaxed);
    return send_frame({dgram.data(), kUdpPrefixSize + frame_len});
}

bool AsyncUdpFeed::publish_heartbeat() {
    HeartbeatMsg hb{};
    std::array<std::byte, kUdpPrefixSize + kMaxFrameSize> dgram{};
    write_u32le(dgram.data(), 0 /*broadcast*/);
    write_u64le(dgram.data() + 4, next_seq_++);

    const std::size_t frame_len = encode(hb, {dgram.data() + kUdpPrefixSize,
                                              dgram.size() - kUdpPrefixSize});
    if (frame_len == 0) return false;

    pkts_sent_.fetch_add(1, std::memory_order_relaxed);
    return send_frame({dgram.data(), kUdpPrefixSize + frame_len});
}

// ─────────────────────────────────────────────────────────────────────────────
// Receive loop
// ─────────────────────────────────────────────────────────────────────────────
void AsyncUdpFeed::recv_loop(std::stop_token st) {
    std::array<std::byte, kMaxDgramSize> buf{};

    while (!st.stop_requested() && running_.load()) {
        const ssize_t n = ::recv(sock_fd_, buf.data(), buf.size(), 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Yield briefly to avoid spinning 100% CPU
                std::this_thread::sleep_for(std::chrono::microseconds(50));
                continue;
            }
            break; // Fatal error
        }
        if (static_cast<std::size_t>(n) < kUdpPrefixSize) continue; // too short

        pkts_recv_.fetch_add(1, std::memory_order_relaxed);
        handle_datagram({buf.data(), static_cast<std::size_t>(n)});
    }
}

void AsyncUdpFeed::handle_datagram(std::span<const std::byte> dgram) {
    if (dgram.size() < kUdpPrefixSize) return;

    const md::SymbolId symbol_id = read_u32le(dgram.data());
    const uint64_t     seq_no    = read_u64le(dgram.data() + 4);
    const std::span<const std::byte> frame_data{dgram.data() + kUdpPrefixSize,
                                                dgram.size() - kUdpPrefixSize};

    // ── Sequence gap detection ────────────────────────────────────────────────
    auto& ss = seq_map_[symbol_id];

    if (seq_no < ss.expected) {
        // Duplicate or very late packet – discard
        return;
    }

    if (seq_no > ss.expected) {
        // Gap detected
        ++gaps_detected_;
        if (gap_cb_) {
            gap_cb_({symbol_id, ss.expected, seq_no});
        }
        // Buffer packet in reorder window
        if (ss.pending.size() < cfg_.reorder_window) {
            ss.pending[seq_no] = std::vector<std::byte>(
                frame_data.begin(), frame_data.end());
        }
        return;
    }

    // seq_no == expected → dispatch this frame
    {
        BinaryParser parser([this](ParseResult r){ dispatch_decoded(r); });
        parser.push(frame_data);
        ++ss.expected;
    }

    // Drain any buffered consecutive frames
    while (true) {
        auto it = ss.pending.find(ss.expected);
        if (it == ss.pending.end()) break;
        BinaryParser parser([this](ParseResult r){ dispatch_decoded(r); });
        parser.push(it->second);
        ss.pending.erase(it);
        ++ss.expected;
    }
}

void AsyncUdpFeed::dispatch_decoded(const ParseResult& result) {
    if (!result.ok) return;
    std::visit([this](auto&& msg) {
        using T = std::decay_t<decltype(msg)>;
        if constexpr (std::is_same_v<T, Tick>) {
            if (tick_cb_) tick_cb_(msg);
        } else if constexpr (std::is_same_v<T, Bar>) {
            if (bar_cb_)  bar_cb_(msg);
        }
    }, result.message);
}

} // namespace net
