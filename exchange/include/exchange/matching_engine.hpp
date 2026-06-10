#pragma once
// exchange/matching_engine.hpp  ── Price-Time Priority Matching Engine
//
// Design:
//  • Processes one incoming Order at a time.
//  • Sweeps contra-side levels from best price inward until:
//      – order fully filled, OR
//      – no more crossable levels remain
//  • Handles all 5 order types:
//      Market  – match at any price; residual impossible (no price barrier)
//      Limit   – match up to limit_price; residual rests in book (GTC/DAY)
//      Stop    – rests unactivated; triggered by MatchingEngine::on_tick()
//      IOC     – match up to limit_price; cancel residual immediately
//      FOK     – pre-check full availability; fill entirely or reject entirely
//  • Partial fills: each price level crossed generates a separate Fill event.
//  • Callbacks fired synchronously in process(); no queuing.
//  • ExecutionSimulator is injected for slippage + commission annotation.

#include "exchange/order_book.hpp"
#include "exchange/types.hpp"
#include "market_data/types.hpp"

#include <atomic>
#include <functional>
#include <unordered_map>

namespace exch {

// Forward declaration
class ExecutionSimulator;

// ── Callbacks ─────────────────────────────────────────────────────────────────
using FillCallback   = std::function<void(const Fill&)>;
using ReportCallback = std::function<void(const ExecutionReport&)>;

// ── MatchingEngine ────────────────────────────────────────────────────────────
class MatchingEngine {
public:
    // ── Construction ─────────────────────────────────────────────────────────
    explicit MatchingEngine(OrderBook& book, ExecutionSimulator& exec_sim);

    // Non-copyable, non-movable (holds references)
    MatchingEngine(const MatchingEngine&)            = delete;
    MatchingEngine& operator=(const MatchingEngine&) = delete;

    // ── Callback registration ─────────────────────────────────────────────────
    void on_fill(FillCallback cb)     { fill_cb_   = std::move(cb); }
    void on_report(ReportCallback cb) { report_cb_ = std::move(cb); }

    // ── Core interface ────────────────────────────────────────────────────────

    /// Process an incoming order through the matching logic.
    /// Modifies `order` in place (filled_qty, status).
    /// Fires fill_cb_ and report_cb_ for each event.
    void process(Order& order, Timestamp now);

    /// Notify the engine of a market data tick.
    /// Used to trigger Stop orders whose stop_price has been crossed.
    void on_tick(const md::Tick& tick, Timestamp now);

    // ── Diagnostics ───────────────────────────────────────────────────────────
    [[nodiscard]] uint64_t fills_generated()  const noexcept { return fills_generated_;  }
    [[nodiscard]] uint64_t orders_processed() const noexcept { return orders_processed_; }

private:
    OrderBook&          book_;
    ExecutionSimulator& exec_sim_;

    FillCallback        fill_cb_;
    ReportCallback      report_cb_;

    std::atomic<FillId>   next_fill_id_{1};
    uint64_t              fills_generated_  {0};
    uint64_t              orders_processed_ {0};

    // ── Stop order registry ───────────────────────────────────────────────────
    // stop_orders_[symbol_id] → list of resting stop order IDs
    std::unordered_map<SymbolId, std::vector<OrderId>> stop_orders_;

    // ── Internal helpers ──────────────────────────────────────────────────────

    /// Market order: sweep all levels until filled or book exhausted.
    void match_market(Order& order, Timestamp now);

    /// Limit order: sweep until limit_price barrier or fully filled.
    /// `rest_on_book` controls whether residual is added to the book.
    void match_limit(Order& order, Timestamp now, bool rest_on_book);

    /// IOC: same as Limit but cancels residual after matching.
    void match_ioc(Order& order, Timestamp now);

    /// FOK: checks full fill availability, then executes or rejects.
    void match_fok(Order& order, Timestamp now);

    /// Core sweep loop: walks contra-side levels, generating fills.
    /// Returns quantity filled.
    Quantity sweep_contra(Order& order, Timestamp now,
                          Price price_limit,   ///< Price ceiling/floor (or 0 = no limit)
                          bool  check_only);   ///< If true, count available qty but don't fill

    /// Generate a Fill for aggressor order vs. resting order.
    Fill make_fill(Order& aggressor, Order& resting,
                   Price fill_price, Quantity fill_qty,
                   Timestamp now);

    /// Emit fill callback + update order states
    void emit_fill(Order& aggressor, Order& resting,
                   Price fill_price, Quantity fill_qty,
                   Timestamp now);

    /// Emit ExecutionReport for an order
    void emit_report(const Order& order, const Fill* fill = nullptr);

    /// Register a Stop order in the watch list
    void register_stop(const Order& order);

    /// Cancel residual quantity of an order
    void cancel_residual(Order& order, Timestamp now);

    /// Price crossing check helpers
    [[nodiscard]] bool buy_crosses(Price limit, Price contra) const noexcept;
    [[nodiscard]] bool sell_crosses(Price limit, Price contra) const noexcept;
};

} // namespace exch
