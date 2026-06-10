// exchange/order_router.cpp  ── OrderRouter implementation

#include "exchange/order_router.hpp"

#include <stdexcept>

namespace exch {

// ── Construction ──────────────────────────────────────────────────────────────
OrderRouter::OrderRouter(ExecutionSimConfig sim_cfg)
    : sim_cfg_(sim_cfg)
{}

// ── register_symbol ───────────────────────────────────────────────────────────
void OrderRouter::register_symbol(SymbolId id) {
    if (symbols_.contains(id))
        throw std::invalid_argument("OrderRouter: symbol already registered");

    auto& ctx = symbols_[id];
    ctx.book     = std::make_unique<OrderBook>(id);
    ctx.exec_sim = std::make_unique<ExecutionSimulator>(sim_cfg_);
    ctx.engine   = std::make_unique<MatchingEngine>(*ctx.book, *ctx.exec_sim);

    wire_callbacks(ctx);
}

// ── wire_callbacks ────────────────────────────────────────────────────────────
void OrderRouter::wire_callbacks(SymbolContext& ctx) {
    ctx.engine->on_fill([this](const Fill& f) {
        total_fills_.fetch_add(1, std::memory_order_relaxed);
        if (fill_cb_) fill_cb_(f);
    });
    ctx.engine->on_report([this](const ExecutionReport& r) {
        if (report_cb_) report_cb_(r);
    });
}

// ── has_symbol ────────────────────────────────────────────────────────────────
bool OrderRouter::has_symbol(SymbolId id) const noexcept {
    return symbols_.contains(id);
}

// ── find_context ─────────────────────────────────────────────────────────────
OrderRouter::SymbolContext* OrderRouter::find_context(SymbolId id) noexcept {
    auto it = symbols_.find(id);
    return (it != symbols_.end()) ? &it->second : nullptr;
}
const OrderRouter::SymbolContext* OrderRouter::find_context(SymbolId id) const noexcept {
    auto it = symbols_.find(id);
    return (it != symbols_.end()) ? &it->second : nullptr;
}

// ── validate ─────────────────────────────────────────────────────────────────
bool OrderRouter::validate(const Order& order) const noexcept {
    if (order.qty == 0)                    return false;
    if (!symbols_.contains(order.symbol_id)) return false;
    if (order.side == Side::Unknown)        return false;
    if (order.type == OrderType::Limit ||
        order.type == OrderType::IOC   ||
        order.type == OrderType::FOK) {
        if (order.limit_price <= 0)        return false;
    }
    if (order.type == OrderType::Stop) {
        if (order.stop_price <= 0)         return false;
    }
    return true;
}

// ── submit ───────────────────────────────────────────────────────────────────
OrderId OrderRouter::submit(Order& order, Timestamp now) {
    if (!validate(order)) {
        order.status        = OrderStatus::Rejected;
        order.reject_reason = RejectReason::InvalidQuantity;
        if (!symbols_.contains(order.symbol_id))
            order.reject_reason = RejectReason::UnknownSymbol;
        if (report_cb_) report_cb_({.order = order});
        return kInvalidOrderId;
    }

    // Assign ID and timestamp
    order.id        = next_order_id_.fetch_add(1, std::memory_order_relaxed);
    order.submit_ts = now;
    order.status    = OrderStatus::New;
    orders_submitted_.fetch_add(1, std::memory_order_relaxed);

    auto* ctx = find_context(order.symbol_id);
    // validated above – ctx must be non-null
    order_symbol_map_[order.id] = order.symbol_id;
    ctx->engine->process(order, now);
    return order.id;
}

// ── Convenience submit overloads ──────────────────────────────────────────────
OrderId OrderRouter::submit_limit(SymbolId sym, Side side, Price price,
                                   Quantity qty, TimeInForce tif, Timestamp now) {
    Order o{};
    o.symbol_id   = sym;
    o.side        = side;
    o.type        = OrderType::Limit;
    o.tif         = tif;
    o.limit_price = price;
    o.qty         = qty;
    return submit(o, now);
}

OrderId OrderRouter::submit_market(SymbolId sym, Side side,
                                    Quantity qty, Timestamp now) {
    Order o{};
    o.symbol_id = sym;
    o.side      = side;
    o.type      = OrderType::Market;
    o.qty       = qty;
    return submit(o, now);
}

OrderId OrderRouter::submit_stop(SymbolId sym, Side side,
                                  Price stop_price, Quantity qty, Timestamp now) {
    Order o{};
    o.symbol_id  = sym;
    o.side       = side;
    o.type       = OrderType::Stop;
    o.stop_price = stop_price;
    o.qty        = qty;
    return submit(o, now);
}

OrderId OrderRouter::submit_ioc(SymbolId sym, Side side, Price limit_price,
                                 Quantity qty, Timestamp now) {
    Order o{};
    o.symbol_id   = sym;
    o.side        = side;
    o.type        = OrderType::IOC;
    o.tif         = TimeInForce::IOC;
    o.limit_price = limit_price;
    o.qty         = qty;
    return submit(o, now);
}

OrderId OrderRouter::submit_fok(SymbolId sym, Side side, Price limit_price,
                                 Quantity qty, Timestamp now) {
    Order o{};
    o.symbol_id   = sym;
    o.side        = side;
    o.type        = OrderType::FOK;
    o.tif         = TimeInForce::FOK;
    o.limit_price = limit_price;
    o.qty         = qty;
    return submit(o, now);
}

// ── cancel ────────────────────────────────────────────────────────────────────
bool OrderRouter::cancel(OrderId id, Timestamp now) {
    auto sym_it = order_symbol_map_.find(id);
    if (sym_it == order_symbol_map_.end()) return false;

    auto* ctx = find_context(sym_it->second);
    if (!ctx) return false;

    const bool ok = ctx->book->cancel(id);
    if (ok) {
        // Emit a cancellation report
        Order dummy{};
        dummy.id     = id;
        dummy.status = OrderStatus::Cancelled;
        dummy.last_fill_ts = now;
        if (report_cb_) report_cb_({.order = dummy});
        order_symbol_map_.erase(sym_it);
    }
    return ok;
}

// ── on_tick ───────────────────────────────────────────────────────────────────
void OrderRouter::on_tick(const md::Tick& tick, Timestamp now) {
    auto* ctx = find_context(tick.symbol_id);
    if (!ctx) return;
    ctx->engine->on_tick(tick, now);
}

// ── book ─────────────────────────────────────────────────────────────────────
const OrderBook* OrderRouter::book(SymbolId id) const noexcept {
    const auto* ctx = find_context(id);
    return ctx ? ctx->book.get() : nullptr;
}

} // namespace exch
