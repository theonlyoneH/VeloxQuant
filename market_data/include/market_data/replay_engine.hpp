#pragma once
// market_data/replay_engine.hpp  ── Historical tick replay with no lookahead
//
// Design:
//  • Owns (or borrows) a BinaryStore<Tick>.
//  • The replay window [start_ts, end_ts) is set at construction.
//  • step() returns the next tick in time order.  It NEVER returns a tick
//    whose recv_ts is >= end_ts.
//  • seek() rewinds or fast-forwards within the valid window.
//  • A speed_multiplier controls wall-clock pacing (1.0 = real-time).
//  • Callbacks are invoked synchronously inside step() – no hidden threads.
//  • A background std::jthread optionally drives autonomous replay at speed.

#include "market_data/binary_store.hpp"
#include "market_data/types.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

namespace md {

// ── ReplayStatus ─────────────────────────────────────────────────────────────
enum class ReplayStatus {
    Running,   ///< Replay is active
    Finished,  ///< Reached end_ts or last record
    Paused,    ///< speed_multiplier == 0 or explicitly paused
};

// ── HistoricalReplayEngine ───────────────────────────────────────────────────
class HistoricalReplayEngine {
public:
    // ── Configuration ─────────────────────────────────────────────────────────
    struct Config {
        Timestamp   start_ts;           ///< Inclusive replay start (ns)
        Timestamp   end_ts;             ///< Exclusive replay end (ns)
        double      speed_multiplier;   ///< Replay speed (1.0 = real-time, 0 = max speed)
        bool        loop;               ///< Wrap around when finished
    };

    // ── Construction ─────────────────────────────────────────────────────────
    /// Takes ownership of the store.
    explicit HistoricalReplayEngine(BinaryStore<Tick> store, Config cfg);

    /// Borrow a store (caller keeps ownership – store must outlive engine).
    explicit HistoricalReplayEngine(std::span<const Tick> view, Config cfg);

    ~HistoricalReplayEngine() noexcept;

    // Move-only
    HistoricalReplayEngine(const HistoricalReplayEngine&)            = delete;
    HistoricalReplayEngine& operator=(const HistoricalReplayEngine&) = delete;
    HistoricalReplayEngine(HistoricalReplayEngine&&)                 = default;
    HistoricalReplayEngine& operator=(HistoricalReplayEngine&&)      = default;

    // ── Subscription ─────────────────────────────────────────────────────────
    /// Register a callback invoked for every replayed tick.
    /// Returns an opaque handle; call unsubscribe(handle) to remove.
    using TickCallback = std::function<void(const Tick&)>;
    using SubHandle    = std::size_t;

    SubHandle subscribe(TickCallback cb);
    void      unsubscribe(SubHandle h);

    // ── Manual step interface ─────────────────────────────────────────────────
    /// Advance by one record.  Fires all callbacks.
    /// Returns the tick if one was available, nullopt if finished.
    std::optional<Tick> step();

    /// Advance until recv_ts >= target_ts (no-lookahead: target_ts < end_ts).
    void step_to(Timestamp target_ts);

    // ── Seek ─────────────────────────────────────────────────────────────────
    /// Reposition the cursor to the first tick with recv_ts >= ts.
    /// ts must be in [start_ts, end_ts).
    void seek(Timestamp ts);

    /// Rewind to the very beginning of the window.
    void rewind();

    // ── Autonomous replay (background thread) ────────────────────────────────
    void start();  ///< Launch jthread at configured speed
    void stop();   ///< Request stop; blocks until thread exits
    void pause();
    void resume();

    // ── Accessors ─────────────────────────────────────────────────────────────
    [[nodiscard]] Timestamp    current_ts()  const noexcept { return current_ts_.load(); }
    [[nodiscard]] ReplayStatus status()      const noexcept;
    [[nodiscard]] std::size_t  cursor()      const noexcept { return cursor_; }
    [[nodiscard]] std::size_t  total_ticks() const noexcept { return view_.size(); }

    // ── Config mutation ───────────────────────────────────────────────────────
    void set_speed(double multiplier) noexcept;
    void set_loop(bool loop)          noexcept { cfg_.loop = loop; }

private:
    // ── Storage ───────────────────────────────────────────────────────────────
    BinaryStore<Tick>    owned_store_;     ///< Optional owned store
    std::span<const Tick> view_;           ///< Non-owning view (either of store or external)

    Config               cfg_;
    std::size_t          cursor_      {0};
    std::size_t          begin_idx_   {0}; ///< First idx with recv_ts >= start_ts
    std::size_t          end_idx_     {0}; ///< First idx with recv_ts >= end_ts

    std::atomic<Timestamp> current_ts_{0};
    std::atomic<bool>      paused_    {false};

    // ── Callbacks ─────────────────────────────────────────────────────────────
    struct Subscription {
        SubHandle    handle;
        TickCallback cb;
    };
    std::vector<Subscription> subs_;
    SubHandle                 next_handle_{0};

    // ── Background thread ─────────────────────────────────────────────────────
    std::jthread replay_thread_;

    // ── Timestamps for real-time pacing ───────────────────────────────────────
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    TimePoint wall_start_;
    Timestamp data_start_ts_{0};

    // ── Private helpers ───────────────────────────────────────────────────────
    void         fire_callbacks(const Tick& t);
    void         init_indices();
    void         run_loop(std::stop_token stoken);
    bool         at_end() const noexcept;
    TimePoint    next_wall_time(Timestamp data_ts) const noexcept;
};

} // namespace md
