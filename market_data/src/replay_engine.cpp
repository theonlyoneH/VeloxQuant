// market_data/replay_engine.cpp  ── HistoricalReplayEngine implementation

#include "market_data/replay_engine.hpp"

#include <algorithm>
#include <cassert>
#include <thread>

namespace md {

// ── Constructor (owned store) ─────────────────────────────────────────────────
HistoricalReplayEngine::HistoricalReplayEngine(BinaryStore<Tick> store, Config cfg)
    : owned_store_(std::move(store))
    , view_(owned_store_.records())
    , cfg_(cfg)
{
    init_indices();
}

// ── Constructor (borrowed span) ───────────────────────────────────────────────
HistoricalReplayEngine::HistoricalReplayEngine(std::span<const Tick> view, Config cfg)
    : view_(view)
    , cfg_(cfg)
{
    init_indices();
}

// ── Destructor ────────────────────────────────────────────────────────────────
HistoricalReplayEngine::~HistoricalReplayEngine() noexcept {
    stop();
}

// ── init_indices ─────────────────────────────────────────────────────────────
void HistoricalReplayEngine::init_indices() {
    // Find first index with recv_ts >= start_ts
    auto it_begin = std::lower_bound(view_.begin(), view_.end(), cfg_.start_ts,
        [](const Tick& t, Timestamp ts){ return t.recv_ts < ts; });
    begin_idx_ = static_cast<std::size_t>(it_begin - view_.begin());

    // Find first index with recv_ts >= end_ts  (exclusive upper bound)
    auto it_end = std::lower_bound(view_.begin(), view_.end(), cfg_.end_ts,
        [](const Tick& t, Timestamp ts){ return t.recv_ts < ts; });
    end_idx_ = static_cast<std::size_t>(it_end - view_.begin());

    cursor_ = begin_idx_;
    current_ts_.store(cfg_.start_ts);
}

// ── subscribe ─────────────────────────────────────────────────────────────────
auto HistoricalReplayEngine::subscribe(TickCallback cb) -> SubHandle {
    SubHandle h = next_handle_++;
    subs_.push_back({h, std::move(cb)});
    return h;
}

// ── unsubscribe ───────────────────────────────────────────────────────────────
void HistoricalReplayEngine::unsubscribe(SubHandle h) {
    subs_.erase(std::remove_if(subs_.begin(), subs_.end(),
        [h](const Subscription& s){ return s.handle == h; }), subs_.end());
}

// ── fire_callbacks ────────────────────────────────────────────────────────────
void HistoricalReplayEngine::fire_callbacks(const Tick& t) {
    for (auto& sub : subs_) sub.cb(t);
}

// ── at_end ───────────────────────────────────────────────────────────────────
bool HistoricalReplayEngine::at_end() const noexcept {
    return cursor_ >= end_idx_;
}

// ── step ─────────────────────────────────────────────────────────────────────
std::optional<Tick> HistoricalReplayEngine::step() {
    if (at_end()) {
        if (cfg_.loop) rewind();
        else           return std::nullopt;
    }

    const Tick& t = view_[cursor_++];
    current_ts_.store(t.recv_ts);
    fire_callbacks(t);
    return t;
}

// ── step_to ──────────────────────────────────────────────────────────────────
void HistoricalReplayEngine::step_to(Timestamp target_ts) {
    // Clamp to window – enforce no lookahead
    target_ts = std::min(target_ts, cfg_.end_ts - 1);

    while (!at_end() && view_[cursor_].recv_ts <= target_ts) {
        const Tick& t = view_[cursor_++];
        current_ts_.store(t.recv_ts);
        fire_callbacks(t);
    }
}

// ── seek ─────────────────────────────────────────────────────────────────────
void HistoricalReplayEngine::seek(Timestamp ts) {
    if (ts < cfg_.start_ts || ts >= cfg_.end_ts)
        throw std::out_of_range("HistoricalReplayEngine::seek: timestamp out of window");

    auto it = std::lower_bound(view_.begin() + static_cast<std::ptrdiff_t>(begin_idx_),
                               view_.begin() + static_cast<std::ptrdiff_t>(end_idx_),
                               ts,
                               [](const Tick& t, Timestamp s){ return t.recv_ts < s; });
    cursor_     = static_cast<std::size_t>(it - view_.begin());
    current_ts_.store(ts);
}

// ── rewind ───────────────────────────────────────────────────────────────────
void HistoricalReplayEngine::rewind() {
    cursor_ = begin_idx_;
    current_ts_.store(cfg_.start_ts);
}

// ── status ───────────────────────────────────────────────────────────────────
ReplayStatus HistoricalReplayEngine::status() const noexcept {
    if (paused_.load()) return ReplayStatus::Paused;
    if (at_end())       return ReplayStatus::Finished;
    return ReplayStatus::Running;
}

// ── set_speed ────────────────────────────────────────────────────────────────
void HistoricalReplayEngine::set_speed(double multiplier) noexcept {
    cfg_.speed_multiplier = multiplier;
}

// ── next_wall_time ────────────────────────────────────────────────────────────
auto HistoricalReplayEngine::next_wall_time(Timestamp data_ts) const noexcept
    -> TimePoint
{
    if (cfg_.speed_multiplier <= 0.0) {
        // Max speed – no pacing
        return wall_start_;
    }
    const double elapsed_data_ns =
        static_cast<double>(data_ts - data_start_ts_);
    const double elapsed_wall_ns =
        elapsed_data_ns / cfg_.speed_multiplier;
    return wall_start_ + std::chrono::nanoseconds(
        static_cast<int64_t>(elapsed_wall_ns));
}

// ── run_loop (background thread body) ────────────────────────────────────────
void HistoricalReplayEngine::run_loop(std::stop_token stoken) {
    wall_start_    = Clock::now();
    data_start_ts_ = (cursor_ < end_idx_) ? view_[cursor_].recv_ts : cfg_.start_ts;

    while (!stoken.stop_requested() && !at_end()) {
        if (paused_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        const Tick& t = view_[cursor_];

        // Real-time pacing
        if (cfg_.speed_multiplier > 0.0) {
            const TimePoint target = next_wall_time(t.recv_ts);
            std::this_thread::sleep_until(target);
        }

        if (stoken.stop_requested()) break;

        ++cursor_;
        current_ts_.store(t.recv_ts);
        fire_callbacks(t);
    }

    if (cfg_.loop && !stoken.stop_requested()) {
        rewind();
        run_loop(stoken); // recurse to replay again
    }
}

// ── start ────────────────────────────────────────────────────────────────────
void HistoricalReplayEngine::start() {
    if (replay_thread_.joinable())
        throw std::logic_error("HistoricalReplayEngine::start already running");
    paused_.store(false);
    replay_thread_ = std::jthread([this](std::stop_token st){ run_loop(st); });
}

// ── stop ─────────────────────────────────────────────────────────────────────
void HistoricalReplayEngine::stop() {
    if (replay_thread_.joinable()) {
        replay_thread_.request_stop();
        replay_thread_.join();
    }
}

// ── pause / resume ───────────────────────────────────────────────────────────
void HistoricalReplayEngine::pause()  { paused_.store(true);  }
void HistoricalReplayEngine::resume() {
    paused_.store(false);
    // Reset wall-clock reference so pacing stays accurate after pause
    if (cursor_ < end_idx_) {
        wall_start_    = Clock::now();
        data_start_ts_ = view_[cursor_].recv_ts;
    }
}

} // namespace md
