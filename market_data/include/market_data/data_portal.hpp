#pragma once
// market_data/data_portal.hpp  ── Point-in-time market data access
//
// Design:
//  • DataPortal is the single front-door for all market data queries.
//  • It enforces the no-lookahead invariant: every query returns only data
//    that was available at or before current_simulation_time().
//  • Multiple BinaryStore<Tick> files can be registered (one per symbol).
//  • Bar construction is done on-demand from raw ticks (or pre-built stores).
//  • Subscriptions are routed through HistoricalReplayEngine callbacks.
//  • The portal is NOT thread-safe; it should be driven by a single simulation
//    thread.  The replay engine's background thread fires callbacks while
//    holding no lock – callers must ensure sequential access.

#include "market_data/binary_store.hpp"
#include "market_data/replay_engine.hpp"
#include "market_data/types.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace md {

// ── BarFrequency ─────────────────────────────────────────────────────────────
struct BarFrequency {
    BarType  type;
    uint32_t count; ///< e.g. count=5, type=Minute → 5-minute bars
};

// ── DataPortal ────────────────────────────────────────────────────────────────
class DataPortal {
public:
    // ── Construction ─────────────────────────────────────────────────────────
    DataPortal() = default;

    /// Convenience: build from a directory of *.ticks binary files.
    /// Files are expected to be named "<symbol>.ticks".
    explicit DataPortal(const std::filesystem::path& data_dir,
                        Timestamp start_ts, Timestamp end_ts);

    // Move-only
    DataPortal(const DataPortal&)            = delete;
    DataPortal& operator=(const DataPortal&) = delete;
    DataPortal(DataPortal&&)                 = default;
    DataPortal& operator=(DataPortal&&)      = default;

    ~DataPortal() = default;

    // ── Data registration ─────────────────────────────────────────────────────
    /// Register a tick store for a symbol; returns assigned SymbolId.
    SymbolId register_store(const std::string& ticker,
                            BinaryStore<Tick>  store);

    /// Register an already-opened store by symbol id.
    void register_store(SymbolId id, const std::string& ticker,
                        BinaryStore<Tick> store);

    // ── Simulation clock ─────────────────────────────────────────────────────
    /// Called by the simulation loop to advance the portal's clock.
    void advance_to(Timestamp ts) noexcept;

    [[nodiscard]] Timestamp current_ts() const noexcept { return current_ts_; }

    // ── Point-in-time queries ─────────────────────────────────────────────────
    /// Most recent tick for symbol at or before current_ts_.
    /// Returns nullopt if no data available yet.
    [[nodiscard]] std::optional<Tick>
    current_tick(SymbolId id) const noexcept;

    /// Overload accepting ticker string.
    [[nodiscard]] std::optional<Tick>
    current_tick(std::string_view ticker) const noexcept;

    /// Most recent mid price (bid+ask)/2 or last trade price.
    [[nodiscard]] std::optional<double>
    current_price(SymbolId id) const noexcept;

    /// Historical ticks in [current_ts - lookback_ns, current_ts].
    /// Guarantees no tick returned has recv_ts > current_ts_.
    [[nodiscard]] std::vector<Tick>
    history(SymbolId id, Timestamp lookback_ns) const;

    /// Build and return the last N bars of the given frequency,
    /// all with close_ts <= current_ts_.
    [[nodiscard]] std::vector<Bar>
    history_bars(SymbolId id, BarFrequency freq, std::size_t n_bars) const;

    // ── Subscription ─────────────────────────────────────────────────────────
    using TickCallback = std::function<void(const Tick&)>;
    using SubHandle    = std::size_t;

    /// Subscribe to real-time tick callbacks for symbol.
    SubHandle subscribe(SymbolId id, TickCallback cb);
    SubHandle subscribe(std::string_view ticker, TickCallback cb);
    void      unsubscribe(SubHandle h);

    // ── Symbol map ────────────────────────────────────────────────────────────
    [[nodiscard]] std::optional<SymbolId> lookup_id(std::string_view ticker) const noexcept;
    [[nodiscard]] std::optional<std::string> lookup_ticker(SymbolId id) const noexcept;
    [[nodiscard]] std::vector<SymbolId> all_symbols() const;

private:
    // ── Internal per-symbol data ──────────────────────────────────────────────
    struct SymbolData {
        std::string            ticker;
        BinaryStore<Tick>      store;
    };

    // ── State ─────────────────────────────────────────────────────────────────
    Timestamp current_ts_{0};
    SymbolId  next_id_   {0};

    std::unordered_map<SymbolId, SymbolData>    by_id_;
    std::unordered_map<std::string, SymbolId>   ticker_map_;

    // ── Subscription tracking ─────────────────────────────────────────────────
    struct PortalSub {
        SymbolId   symbol_id;
        SubHandle  portal_handle;
        TickCallback cb;
    };
    SubHandle              next_handle_{0};
    std::vector<PortalSub> subscriptions_;

    // ── Bar building ──────────────────────────────────────────────────────────
    [[nodiscard]] std::vector<Bar>
    build_bars(std::span<const Tick> ticks,
               BarFrequency freq, std::size_t n_bars) const;

    [[nodiscard]] int64_t bar_duration_ns(BarFrequency freq) const noexcept;
};

} // namespace md
