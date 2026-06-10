// networking/tcp_feed.cpp  ── Async TCP feed implementation (epoll, Linux)

#include "networking/tcp_feed.hpp"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <system_error>

// Linux headers
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace net {

// ─────────────────────────────────────────────────────────────────────────────
// SpscRingBuffer
// ─────────────────────────────────────────────────────────────────────────────

SpscRingBuffer::SpscRingBuffer(std::size_t capacity) {
    // Round up to next power-of-two
    std::size_t sz = 1;
    while (sz < capacity) sz <<= 1;
    data_.resize(sz);
    mask_ = sz - 1;
}

std::size_t SpscRingBuffer::try_write(std::span<const std::byte> data) noexcept {
    const std::size_t avail = available_write();
    const std::size_t n     = std::min(data.size(), avail);
    if (n == 0) return 0;

    const std::size_t h    = head_.load(std::memory_order_relaxed);
    const std::size_t cap  = data_.size();
    const std::size_t wrap = cap - (h & mask_);

    if (n <= wrap) {
        std::memcpy(data_.data() + (h & mask_), data.data(), n);
    } else {
        std::memcpy(data_.data() + (h & mask_), data.data(), wrap);
        std::memcpy(data_.data(), data.data() + wrap, n - wrap);
    }
    head_.store(h + n, std::memory_order_release);
    return n;
}

std::size_t SpscRingBuffer::try_read(std::span<std::byte> out) noexcept {
    const std::size_t avail = available_read();
    const std::size_t n     = std::min(out.size(), avail);
    if (n == 0) return 0;

    const std::size_t t    = tail_.load(std::memory_order_relaxed);
    const std::size_t cap  = data_.size();
    const std::size_t wrap = cap - (t & mask_);

    if (n <= wrap) {
        std::memcpy(out.data(), data_.data() + (t & mask_), n);
    } else {
        std::memcpy(out.data(), data_.data() + (t & mask_), wrap);
        std::memcpy(out.data() + wrap, data_.data(), n - wrap);
    }
    tail_.store(t + n, std::memory_order_release);
    return n;
}

std::size_t SpscRingBuffer::available_read() const noexcept {
    return head_.load(std::memory_order_acquire)
         - tail_.load(std::memory_order_relaxed);
}

std::size_t SpscRingBuffer::available_write() const noexcept {
    return data_.size() - available_read();
}

bool SpscRingBuffer::empty() const noexcept {
    return available_read() == 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// AsyncTcpFeed helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace {

void throw_errno(const char* ctx) {
    throw std::system_error(errno, std::generic_category(), ctx);
}

} // anon namespace

// ─────────────────────────────────────────────────────────────────────────────
// AsyncTcpFeed – construction / destruction
// ─────────────────────────────────────────────────────────────────────────────
AsyncTcpFeed::AsyncTcpFeed(Config cfg)
    : cfg_(std::move(cfg))
{}

AsyncTcpFeed::~AsyncTcpFeed() noexcept {
    stop();
    if (epoll_fd_ >= 0) { ::close(epoll_fd_); epoll_fd_ = -1; }
    if (listen_fd_ >= 0){ ::close(listen_fd_); listen_fd_ = -1; }
    if (conn_fd_ >= 0)  { ::close(conn_fd_);  conn_fd_  = -1; }
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────
void AsyncTcpFeed::start() {
    if (running_.load())
        throw std::logic_error("AsyncTcpFeed already running");

    epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) throw_errno("epoll_create1");

    running_.store(true);

    if (cfg_.mode == FeedMode::Server) {
        io_thread_ = std::jthread([this](std::stop_token st){ run_server(st); });
    } else {
        io_thread_ = std::jthread([this](std::stop_token st){ run_client(st); });
    }
}

void AsyncTcpFeed::stop() {
    running_.store(false);
    if (io_thread_.joinable()) {
        io_thread_.request_stop();
        io_thread_.join();
    }
}

bool AsyncTcpFeed::is_running() const noexcept {
    return running_.load();
}

std::size_t AsyncTcpFeed::connected_clients() const noexcept {
    return clients_.size();
}

// ─────────────────────────────────────────────────────────────────────────────
// Socket helpers
// ─────────────────────────────────────────────────────────────────────────────
void AsyncTcpFeed::set_nonblocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) throw_errno("fcntl F_GETFL");
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        throw_errno("fcntl F_SETFL O_NONBLOCK");
}

void AsyncTcpFeed::set_tcp_nodelay(int fd) {
    int on = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
}

// ─────────────────────────────────────────────────────────────────────────────
// Server mode
// ─────────────────────────────────────────────────────────────────────────────
void AsyncTcpFeed::setup_listen_socket() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) throw_errno("socket");

    int on = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));

    set_nonblocking(listen_fd_);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(cfg_.port);
    ::inet_pton(AF_INET, cfg_.host.c_str(), &addr.sin_addr);

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        throw_errno("bind");
    if (::listen(listen_fd_, cfg_.backlog) < 0)
        throw_errno("listen");

    // Add listen fd to epoll
    epoll_event ev{};
    ev.events   = EPOLLIN | EPOLLET;
    ev.data.fd  = listen_fd_;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &ev) < 0)
        throw_errno("epoll_ctl ADD listen_fd");
}

void AsyncTcpFeed::accept_client() {
    while (true) {
        sockaddr_in peer{};
        socklen_t   len = sizeof(peer);
        int cfd = ::accept4(listen_fd_,
                            reinterpret_cast<sockaddr*>(&peer), &len,
                            SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (cfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            // Other errors: log and continue
            break;
        }

        set_tcp_nodelay(cfd);

        char addr_buf[INET_ADDRSTRLEN]{};
        ::inet_ntop(AF_INET, &peer.sin_addr, addr_buf, sizeof(addr_buf));
        const uint16_t remote_port = ntohs(peer.sin_port);

        auto cb = make_parser_cb();
        auto state = std::make_unique<ClientState>(
            cfd, addr_buf, remote_port, cfg_.ring_capacity, std::move(cb));

        // Register with epoll (EPOLLIN for reads + EPOLLOUT for backpressure)
        epoll_event ev{};
        ev.events  = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
        ev.data.fd = cfd;
        ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, cfd, &ev);

        if (conn_cb_)
            conn_cb_({cfd, addr_buf, remote_port});

        clients_.emplace(cfd, std::move(state));
    }
}

void AsyncTcpFeed::remove_client(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) return;

    ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    if (disconn_cb_)
        disconn_cb_({fd, it->second->addr, it->second->port});
    ::close(fd);
    clients_.erase(it);
}

void AsyncTcpFeed::drain_send_rings() {
    for (auto& [fd, state] : clients_) {
        std::array<std::byte, 4096> tmp{};
        while (!state->send_ring.empty()) {
            const std::size_t n = state->send_ring.try_read(tmp);
            if (n == 0) break;
            const ssize_t sent = ::send(fd, tmp.data(), n, MSG_NOSIGNAL);
            if (sent < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    // Mark for removal after loop
                }
                break;
            }
        }
    }
}

void AsyncTcpFeed::read_client(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) return;

    std::array<std::byte, 4096> buf{};
    while (true) {
        const ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);
        if (n <= 0) {
            if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
                remove_client(fd);
            break;
        }
        it->second->parser.push({buf.data(), static_cast<std::size_t>(n)});
    }
}

bool AsyncTcpFeed::enqueue_to_all(std::span<const std::byte> frame) {
    bool ok = true;
    for (auto& [fd, state] : clients_) {
        if (state->send_ring.try_write(frame) < frame.size()) ok = false;
    }
    return ok;
}

BinaryParser::MessageCallback AsyncTcpFeed::make_parser_cb() {
    return [this](ParseResult result) {
        if (!result.ok) return;
        std::visit([this](auto&& msg) {
            using T = std::decay_t<decltype(msg)>;
            if constexpr (std::is_same_v<T, Tick>) {
                ticks_recv_.fetch_add(1, std::memory_order_relaxed);
                if (tick_cb_) tick_cb_(msg);
            } else if constexpr (std::is_same_v<T, Bar>) {
                if (bar_cb_) bar_cb_(msg);
            }
        }, result.message);
    };
}

void AsyncTcpFeed::run_server(std::stop_token st) {
    setup_listen_socket();

    constexpr int kMaxEvents = 64;
    std::array<epoll_event, kMaxEvents> events{};

    while (!st.stop_requested() && running_.load()) {
        const int n = ::epoll_wait(epoll_fd_, events.data(), kMaxEvents, 10 /*ms*/);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }

        for (int i = 0; i < n; ++i) {
            const int fd = events[i].data.fd;
            const uint32_t ev = events[i].events;

            if (fd == listen_fd_) {
                if (ev & EPOLLIN) accept_client();
            } else {
                if (ev & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                    remove_client(fd);
                } else {
                    if (ev & EPOLLIN)  read_client(fd);
                    if (ev & EPOLLOUT) drain_send_rings();
                }
            }
        }

        // Also drain send rings on each loop iteration (for published data)
        drain_send_rings();
    }

    // Cleanup
    for (auto& [fd, _] : clients_) {
        ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
        ::close(fd);
    }
    clients_.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Client mode
// ─────────────────────────────────────────────────────────────────────────────
int AsyncTcpFeed::connect_to_server() {
    addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res{};
    const std::string port_str = std::to_string(cfg_.port);
    if (::getaddrinfo(cfg_.host.c_str(), port_str.c_str(), &hints, &res) != 0)
        return -1;

    int fd = ::socket(res->ai_family,
                      res->ai_socktype | SOCK_CLOEXEC, res->ai_protocol);
    if (fd < 0) { ::freeaddrinfo(res); return -1; }

    set_tcp_nodelay(fd);
    set_nonblocking(fd);

    const int rc = ::connect(fd, res->ai_addr, res->ai_addrlen);
    ::freeaddrinfo(res);

    if (rc < 0 && errno != EINPROGRESS) {
        ::close(fd);
        return -1;
    }
    return fd;
}

void AsyncTcpFeed::run_client(std::stop_token st) {
    auto delay = cfg_.reconnect_base_delay;
    int  attempts = 0;

    while (!st.stop_requested() && running_.load()) {
        // Connect
        conn_fd_ = connect_to_server();
        if (conn_fd_ < 0) {
            if (cfg_.max_reconnect_attempts >= 0 &&
                ++attempts > cfg_.max_reconnect_attempts) break;
            std::this_thread::sleep_for(delay);
            delay = std::min(delay * 2, std::chrono::milliseconds(5000));
            continue;
        }
        attempts = 0;
        delay    = cfg_.reconnect_base_delay;

        // Wait for connect completion via epoll
        {
            epoll_event ev{};
            ev.events  = EPOLLOUT | EPOLLET;
            ev.data.fd = conn_fd_;
            ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, conn_fd_, &ev);

            epoll_event out{};
            const int n = ::epoll_wait(epoll_fd_, &out, 1, 2000);
            if (n <= 0 || (out.events & EPOLLERR)) {
                ::close(conn_fd_); conn_fd_ = -1;
                continue;
            }

            // Check SO_ERROR
            int err = 0;
            socklen_t len = sizeof(err);
            ::getsockopt(conn_fd_, SOL_SOCKET, SO_ERROR, &err, &len);
            if (err != 0) {
                ::close(conn_fd_); conn_fd_ = -1;
                continue;
            }
        }

        // Re-arm epoll for read
        {
            epoll_event ev{};
            ev.events  = EPOLLIN | EPOLLET | EPOLLRDHUP;
            ev.data.fd = conn_fd_;
            ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, conn_fd_, &ev);
        }

        const ConnectionInfo ci{conn_fd_, cfg_.host, cfg_.port};
        if (conn_cb_) conn_cb_(ci);

        BinaryParser parser(make_parser_cb());

        // Read loop
        std::array<std::byte, 4096> buf{};
        while (!st.stop_requested() && running_.load()) {
            epoll_event ev{};
            const int n = ::epoll_wait(epoll_fd_, &ev, 1, 10);
            if (n < 0) break;
            if (n == 0) continue;
            if (ev.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) break;
            if (ev.events & EPOLLIN) {
                while (true) {
                    const ssize_t r = ::recv(conn_fd_, buf.data(), buf.size(), 0);
                    if (r <= 0) goto disconnected;
                    parser.push({buf.data(), static_cast<std::size_t>(r)});
                }
            }
        }
        disconnected:
        if (disconn_cb_) disconn_cb_(ci);
        ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, conn_fd_, nullptr);
        ::close(conn_fd_);
        conn_fd_ = -1;

        if (!running_.load() || st.stop_requested()) break;
        std::this_thread::sleep_for(delay);
        delay = std::min(delay * 2, std::chrono::milliseconds(5000));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Publish
// ─────────────────────────────────────────────────────────────────────────────
bool AsyncTcpFeed::publish(const md::Tick& tick) {
    std::array<std::byte, kMaxFrameSize> buf{};
    const std::size_t n = encode(tick, buf);
    if (n == 0) return false;
    ticks_sent_.fetch_add(1, std::memory_order_relaxed);
    return enqueue_to_all({buf.data(), n});
}

bool AsyncTcpFeed::publish(const md::Bar& bar) {
    std::array<std::byte, kMaxFrameSize> buf{};
    const std::size_t n = encode(bar, buf);
    return n > 0 && enqueue_to_all({buf.data(), n});
}

bool AsyncTcpFeed::publish_heartbeat() {
    HeartbeatMsg hb{};
    std::array<std::byte, kMaxFrameSize> buf{};
    const std::size_t n = encode(hb, buf);
    return n > 0 && enqueue_to_all({buf.data(), n});
}

} // namespace net
