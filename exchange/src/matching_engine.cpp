// exchange/matching_engine.cpp  ── Price-Time Priority Matching Engine

#include "exchange/matching_engine.hpp"
#include "exchange/execution_sim.hpp"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace exch {

// ── Construction ──────────────────────────────────────────────────────────────
MatchingEngine::MatchingEngine(OrderBook& book, ExecutionSimulator& exec_sim)
    : book_(book), exec_sim_(exec_sim)
{}

// ── process ───────────────────────────────────────────────────────────────────
void MatchingEngine::process(Order& order, Timestamp now) {
    ++orders_processed_;
    order.submit_ts = now;

    switch (order.type) {
    case OrderType::Market:
        match_market(order, now);
        break;
    case OrderType::Limit:
        match_limit(order, now, /*rest_on_book=*/true);
        break;
    case OrderType::IOC:
        match_ioc(order, now);
        break;
    case OrderType::FOK:
        match_fok(order, now);
        break;
    case OrderType::Stop:
        // Stop: rest on the stop watch list – do not hit the book yet
        register_stop(order);
        emit_report(order);
        break;
    }
}

// ── on_tick ───────────────────────────────────────────────────────────────────
void MatchingEngine::on_tick(const md::Tick& tick, Timestamp now) {
    auto it = stop_orders_.find(tick.symbol_id);
    if (it == stop_orders_.end()) return;

    const Price last = tick.last_price > 0 ? tick.last_price : tick.bid_price;
    std::vector<OrderId> triggered;

    for (OrderId oid : it->second) {
        Order* ord = book_.find_mut(oid);
        if (!ord) continue;  // already gone

        bool trigger = false;
        if (ord->is_buy()  && last >= ord->stop_price) trigger = true;
        if (ord->is_sell() && last <= ord->stop_price) trigger = true;

        if (trigger) {
            triggered.push_back(oid);
        }
    }

    // Activate triggered stops: convert to Market and process
    for (OrderId oid : triggered) {
        it->second.erase(
            std::remove(it->second.begin(), it->second.end(), oid),
            it->second.end());

        Order* ord = book_.find_mut(oid);
        if (!ord) continue;

        ord->type   = OrderType::Market;
        ord->status = OrderStatus::Triggered;
        emit_report(*ord);

        // Remove from book (was resting as stop), then sweep as market
        book_.cancel(oid); // removes from book's structures
        ord->status = OrderStatus::New;  // reset for market sweep
        match_market(*ord, now);
    }
}

// ── match_market ─────────────────────────────────────────────────────────────
void MatchingEngine::match_market(Order& order, Timestamp now) {
    // No price limit – sweep entire contra side
    sweep_contra(order, now, /*price_limit=*/0, /*check_only=*/false);

    if (order.leaves_qty() > 0) {
        // No resting liquidity: cancel residual (market can't rest)
        cancel_residual(order, now);
    }
    emit_report(order);
}

// ── match_limit ───────────────────────────────────────────────────────────────
void MatchingEngine::match_limit(Order& order, Timestamp now, bool rest_on_book) {
    sweep_contra(order, now, order.limit_price, /*check_only=*/false);

    if (order.leaves_qty() > 0) {
        if (rest_on_book) {
            // Residual rests passively in the book
            book_.add(order);
        } else {
            cancel_residual(order, now);
        }
    }

    if (!order.is_done()) emit_report(order);
}

// ── match_ioc ────────────────────────────────────────────────────────────────
void MatchingEngine::match_ioc(Order& order, Timestamp now) {
    // IOC = Limit + cancel residual (no resting)
    order.type = OrderType::Limit;  // reuse limit sweep
    match_limit(order, now, /*rest_on_book=*/false);
    order.type = OrderType::IOC;
}

// ── match_fok ────────────────────────────────────────────────────────────────
void MatchingEngine::match_fok(Order& order, Timestamp now) {
    // 1. Check: how much liquidity is available within limit?
    const Quantity available =
        sweep_contra(order, now, order.limit_price, /*check_only=*/true);

    if (available < order.qty) {
        // Insufficient – reject entire order
        order.status        = OrderStatus::Rejected;
        order.reject_reason = RejectReason::InsufficientLiquidity;
        emit_report(order);
        return;
    }

    // 2. Full liquidity available – execute
    order.type = OrderType::Limit;
    sweep_contra(order, now, order.limit_price, /*check_only=*/false);
    order.type = OrderType::FOK;

    emit_report(order);
}

// ── sweep_contra ─────────────────────────────────────────────────────────────
Quantity MatchingEngine::sweep_contra(Order&    order,
                                       Timestamp now,
                                       Price     price_limit,
                                       bool      check_only) {
    Quantity filled_total = 0;

    const bool is_buy = order.is_buy();

    // Snapshot the arrival price for slippage calc (best contra quote)
    Price arrival_price = 0;
    if (is_buy) {
        auto p = book_.best_ask();
        arrival_price = p.value_or(0);
    } else {
        auto p = book_.best_bid();
        arrival_price = p.value_or(0);
    }

    // Snapshot depth for slippage annotation
    const std::size_t kDepthSnap = 10;
    std::vector<PriceLevel> depth_snap =
        is_buy ? book_.ask_depth(kDepthSnap) : book_.bid_depth(kDepthSnap);

    if (check_only) {
        Quantity available = 0;
        Quantity needed = order.leaves_qty();
        if (is_buy) {
            for (const auto& [ask_price, queue] : book_.asks().levels) {
                if (needed == 0) break;
                if (price_limit > 0 && ask_price > price_limit) break;
                for (OrderId oid : queue) {
                    if (needed == 0) break;
                    const Order* resting = book_.find(oid);
                    if (resting) {
                        Quantity qty = std::min(needed, resting->leaves_qty());
                        available += qty;
                        needed -= qty;
                    }
                }
            }
        } else {
            for (const auto& [bid_price, queue] : book_.bids().levels) {
                if (needed == 0) break;
                if (price_limit > 0 && bid_price < price_limit) break;
                for (OrderId oid : queue) {
                    if (needed == 0) break;
                    const Order* resting = book_.find(oid);
                    if (resting) {
                        Quantity qty = std::min(needed, resting->leaves_qty());
                        available += qty;
                        needed -= qty;
                    }
                }
            }
        }
        return available;
    }

    // Real execution (check_only == false)
    if (is_buy) {
        while (order.leaves_qty() > 0) {
            auto ask_price_opt = book_.best_ask();
            if (!ask_price_opt) break;
            Price ask_price = *ask_price_opt;
            if (price_limit > 0 && ask_price > price_limit) break;

            auto* queue = book_.asks().orders_at_mut(ask_price);
            if (!queue || queue->empty()) break;

            OrderId resting_id = queue->front();
            Order* resting = book_.find_mut(resting_id);
            if (!resting) {
                queue->pop_front();
                continue;
            }

            const Quantity fill_qty = std::min(order.leaves_qty(), resting->leaves_qty());
            filled_total += fill_qty;

            emit_fill(order, *resting, ask_price, fill_qty, now);
            book_.reduce_qty(resting_id, fill_qty);
        }
    } else {
        while (order.leaves_qty() > 0) {
            auto bid_price_opt = book_.best_bid();
            if (!bid_price_opt) break;
            Price bid_price = *bid_price_opt;
            if (price_limit > 0 && bid_price < price_limit) break;

            auto* queue = book_.bids().orders_at_mut(bid_price);
            if (!queue || queue->empty()) break;

            OrderId resting_id = queue->front();
            Order* resting = book_.find_mut(resting_id);
            if (!resting) {
                queue->pop_front();
                continue;
            }

            const Quantity fill_qty = std::min(order.leaves_qty(), resting->leaves_qty());
            filled_total += fill_qty;

            emit_fill(order, *resting, bid_price, fill_qty, now);
            book_.reduce_qty(resting_id, fill_qty);
        }
    }

    // Annotate fills with slippage/commission (only after real execution)
    // Note: annotation is done inside emit_fill via exec_sim_.
    (void)arrival_price;
    (void)depth_snap;

    return filled_total;
}

// ── emit_fill ────────────────────────────────────────────────────────────────
void MatchingEngine::emit_fill(Order& aggressor, Order& resting,
                                Price fill_price, Quantity fill_qty,
                                Timestamp now) {
    ++fills_generated_;

    // ── Aggressor fill ────────────────────────────────────────────────────────
    Fill agg_fill{};
    agg_fill.id           = next_fill_id_.fetch_add(1, std::memory_order_relaxed);
    agg_fill.order_id     = aggressor.id;
    agg_fill.symbol_id    = aggressor.symbol_id;
    agg_fill.side         = aggressor.side;
    agg_fill.fill_price   = fill_price;
    agg_fill.fill_qty     = fill_qty;
    agg_fill.timestamp    = now;
    agg_fill.is_aggressive= true;

    // Compute commission for aggressor
    agg_fill.commission = exec_sim_.compute_commission(fill_qty, fill_price);

    aggressor.filled_qty += fill_qty;
    aggressor.last_fill_ts= now;
    if (aggressor.filled_qty >= aggressor.qty)
        aggressor.status = OrderStatus::Filled;
    else
        aggressor.status = OrderStatus::PartiallyFilled;

    if (fill_cb_) fill_cb_(agg_fill);

    ExecutionReport agg_rpt{.order = aggressor, .last_fill = agg_fill, .has_fill = true};
    if (report_cb_) report_cb_(agg_rpt);

    // ── Resting fill ──────────────────────────────────────────────────────────
    Fill rest_fill{};
    rest_fill.id           = next_fill_id_.fetch_add(1, std::memory_order_relaxed);
    rest_fill.order_id     = resting.id;
    rest_fill.symbol_id    = resting.symbol_id;
    rest_fill.side         = resting.side;
    rest_fill.fill_price   = fill_price;
    rest_fill.fill_qty     = fill_qty;
    rest_fill.timestamp    = now;
    rest_fill.is_aggressive= false;
    rest_fill.commission   = exec_sim_.compute_commission(fill_qty, fill_price);

    // Update resting order state (reduce_qty handles book cleanup)
    resting.last_fill_ts = now;
    if ((resting.filled_qty + fill_qty) >= resting.qty)
        resting.status = OrderStatus::Filled;
    else
        resting.status = OrderStatus::PartiallyFilled;

    if (fill_cb_) fill_cb_(rest_fill);

    ExecutionReport rest_rpt{.order = resting, .last_fill = rest_fill, .has_fill = true};
    if (report_cb_) report_cb_(rest_rpt);
}

// ── emit_report ───────────────────────────────────────────────────────────────
void MatchingEngine::emit_report(const Order& order, const Fill* fill) {
    if (!report_cb_) return;
    ExecutionReport rpt{};
    rpt.order    = order;
    rpt.has_fill = (fill != nullptr);
    if (fill) rpt.last_fill = *fill;
    report_cb_(rpt);
}

// ── register_stop ─────────────────────────────────────────────────────────────
void MatchingEngine::register_stop(const Order& order) {
    // Stop orders rest at their stop_price level (not the limit book)
    // We use a sentinel price of stop_price for book storage so cancel works
    Order stored  = order;
    stored.limit_price = order.stop_price;  // borrow limit slot for storage
    book_.add(stored);
    stop_orders_[order.symbol_id].push_back(order.id);
}

// ── cancel_residual ───────────────────────────────────────────────────────────
void MatchingEngine::cancel_residual(Order& order, Timestamp now) {
    if (order.filled_qty == 0)
        order.status = OrderStatus::Cancelled;
    else if (order.filled_qty < order.qty)
        order.status = OrderStatus::Cancelled;  // partial + cancel = cancelled
    order.last_fill_ts = now;
}

// ── price cross helpers ───────────────────────────────────────────────────────
bool MatchingEngine::buy_crosses(Price limit, Price contra) const noexcept {
    return limit == 0 || contra <= limit;
}
bool MatchingEngine::sell_crosses(Price limit, Price contra) const noexcept {
    return limit == 0 || contra >= limit;
}

} // namespace exch
