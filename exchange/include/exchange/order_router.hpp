#pragma once
// exchange/order_router.hpp  ── Front-door for all order submission
//
// Design:
//  • OrderRouter owns the OrderBook and MatchingEngine for each symbol.
//  • submit(Order&) is the single entry point; it:
//      1. Validates the order (qty > 0, symbol registered, etc.)
//      2. Assigns a unique OrderId (atomic counter)
//      3. Sets submit_ts
//      4. Dispatches to MatchingEngine::process()
//  • cancel(OrderId) routes to the relevant book's cancel().
//  • stop_trigger(Tick&) notifies all engines of a price update so
//    that stop orders can be activated.
//  • Multiple symbols supported; each has its own OrderBook + MatchingEngine.
//  • Callbacks (on_fill, on_report) are shared across all symbols.

#include "exchange/execution_sim.hpp"
#include "exchange/matching_engine.hpp"
#include "exchange/order_book.hpp"
#include "exchange/types.hpp"
#include "market_data/types.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <unordered_map>

namespace exch {

// ── OrderRouter ───────────────────────────────────────────────────────────────
class OrderRouter {
public:
    // ── Construction ─────────────────────────────────────────────────────────
    explicit OrderRouter(ExecutionSimConfig sim_cfg = {});

    // Non-copyable (owns unique_ptrs)
    OrderRouter(const OrderRouter&)            = delete;
    OrderRouter& operator=(const OrderRouter&) = delete;

    // ── Symbol registration ───────────────────────────────────────────────────
    /// Register a symbol so orders can be routed to it.
    void register_symbol(SymbolId id);

    /// Check whether a symbol is registered.
    [[nodiscard]] bool has_symbol(SymbolId id) const noexcept;

    // ── Callback registration ─────────────────────────────────────────────────
    void on_fill(FillCallback cb)     { fill_cb_   = std::move(cb); }
    void on_report(ReportCallback cb) { report_cb_ = std::move(cb); }

    // ── Order submission ──────────────────────────────────────────────────────

    /// Submit a new order.  Returns assigned OrderId, or kInvalidOrderId on
    /// immediate rejection.  The `order` struct is mutated in place.
    OrderId submit(Order& order, Timestamp now);

    /// Convenience: build and submit from parameters.
    OrderId submit_limit(SymbolId sym, Side side, Price price, Quantity qty,
                         TimeInForce tif, Timestamp now);

    OrderId submit_market(SymbolId sym, Side side, Quantity qty, Timestamp now);

    OrderId submit_stop(SymbolId sym, Side side,
                        Price stop_price, Quantity qty, Timestamp now);

    OrderId submit_ioc(SymbolId sym, Side side, Price limit_price,
                       Quantity qty, Timestamp now);

    OrderId submit_fok(SymbolId sym, Side side, Price limit_price,
                       Quantity qty, Timestamp now);

    // ── Cancel ────────────────────────────────────────────────────────────────
    /// Request cancellation of an order.  Returns true if found and cancelled.
    bool cancel(OrderId id, Timestamp now);

    // ── Market data feed ──────────────────────────────────────────────────────
    /// Feed a tick to all engines (Stop order activation).
    void on_tick(const md::Tick& tick, Timestamp now);

    // ── Book access ───────────────────────────────────────────────────────────
    /// Read-only access to a symbol's order book (for strategy / DataPortal use).
    [[nodiscard]] const OrderBook* book(SymbolId id) const noexcept;

    // ── Diagnostics ───────────────────────────────────────────────────────────
    [[nodiscard]] uint64_t total_orders_submitted() const noexcept {
        return orders_submitted_.load();
    }
    [[nodiscard]] uint64_t total_fills()     const noexcept { return total_fills_.load(); }
    [[nodiscard]] OrderId  last_order_id()   const noexcept { return next_order_id_.load() - 1; }

private:
    // ── Per-symbol context ────────────────────────────────────────────────────
    struct SymbolContext {
        std::unique_ptr<OrderBook>       book;
        std::unique_ptr<ExecutionSimulator> exec_sim;
        std::unique_ptr<MatchingEngine>  engine;

        // fast lookup: OrderId → symbol_id  (for cancel routing)
    };

    std::unordered_map<SymbolId, SymbolContext> symbols_;

    // Maps OrderId → SymbolId for O(1) cancel routing
    std::unordered_map<OrderId, SymbolId> order_symbol_map_;

    ExecutionSimConfig  sim_cfg_;
    FillCallback        fill_cb_;
    ReportCallback      report_cb_;

    std::atomic<OrderId>    next_order_id_     {1};
    std::atomic<uint64_t>   orders_submitted_  {0};
    std::atomic<uint64_t>   total_fills_       {0};

    // ── Helpers ───────────────────────────────────────────────────────────────
    SymbolContext* find_context(SymbolId id) noexcept;
    const SymbolContext* find_context(SymbolId id) const noexcept;

    void wire_callbacks(SymbolContext& ctx);

    [[nodiscard]] bool validate(const Order& order) const noexcept;
};

} // namespace exch
