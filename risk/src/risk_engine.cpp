// risk/risk_engine.cpp  ── RiskEngine implementation

#include "risk/risk_engine.hpp"
#include "market_data/types.hpp"

#include <cmath>
#include <format>
#include <sstream>

namespace risk {

// ── Construction ──────────────────────────────────────────────────────────────
RiskEngine::RiskEngine(RiskLimits limits) noexcept
    : limits_(limits)
{}

// ── fire_breach ───────────────────────────────────────────────────────────────
void RiskEngine::fire_breach(BreachEvent ev) {
    if (breach_cb_) breach_cb_(ev);
}

// ── reset_daily_pnl ──────────────────────────────────────────────────────────
void RiskEngine::reset_daily_pnl(Timestamp ts) noexcept {
    daily_pnl_       = 0.0;
    session_start_ts_= ts;
}

// ── check_order ───────────────────────────────────────────────────────────────
RiskCheckResult RiskEngine::check_order(const Order&     order,
                                          const Portfolio& portfolio,
                                          const PriceMap&  prices,
                                          Timestamp        now) {
    (void)now;

    if (auto r = check_kill_switch();               !r.ok) return r;
    if (auto r = check_order_qty(order);             !r.ok) return r;
    if (auto r = check_order_notional(order, prices);!r.ok) return r;
    if (auto r = check_position_limit(order, portfolio); !r.ok) return r;
    if (auto r = check_exposure(order, portfolio, prices);!r.ok) return r;
    if (auto r = check_daily_loss();                 !r.ok) return r;
    if (auto r = check_drawdown();                   !r.ok) return r;

    return RiskCheckResult::pass();
}

// ── on_fill ───────────────────────────────────────────────────────────────────
void RiskEngine::on_fill(const Fill&      fill,
                          const Portfolio& portfolio,
                          const PriceMap&  prices,
                          Timestamp        now) {
    const double current_nav = portfolio.nav(prices);

    // Update HWM and drawdown
    if (current_nav > hwm_) hwm_ = current_nav;
    if (hwm_ > 0.0) {
        drawdown_ = (hwm_ - current_nav) / hwm_;
        if (drawdown_ > limits_.max_drawdown_pct) {
            fire_breach({
                .type          = BreachType::DrawdownLimit,
                .detail        = "Drawdown exceeded",
                .timestamp     = now,
                .current_value = drawdown_,
                .limit_value   = limits_.max_drawdown_pct,
            });
        }
    }

    // Update daily P&L
    if (prev_nav_ > 0.0) {
        daily_pnl_ += current_nav - prev_nav_;
    }
    prev_nav_ = current_nav;

    if (daily_pnl_ < -limits_.max_daily_loss) {
        fire_breach({
            .type          = BreachType::DailyLossLimit,
            .detail        = "Daily loss exceeded",
            .timestamp     = now,
            .current_value = daily_pnl_,
            .limit_value   = -limits_.max_daily_loss,
        });
    }

    (void)fill;
}

// ── Individual checks ─────────────────────────────────────────────────────────

RiskCheckResult RiskEngine::check_kill_switch() const noexcept {
    if (limits_.kill_switch_enabled)
        return RiskCheckResult::fail("Kill switch active – all orders rejected");
    return RiskCheckResult::pass();
}

RiskCheckResult RiskEngine::check_order_qty(const Order& order) const noexcept {
    if (order.qty > limits_.max_order_qty) {
        std::ostringstream ss;
        ss << "Order qty " << order.qty
           << " exceeds max_order_qty " << limits_.max_order_qty;
        return RiskCheckResult::fail(ss.str());
    }
    return RiskCheckResult::pass();
}

RiskCheckResult RiskEngine::check_order_notional(const Order&    order,
                                                   const PriceMap& prices) const noexcept {
    if (order.limit_price > 0) {
        const double notional = md::from_price(order.limit_price)
                              * static_cast<double>(order.qty);
        if (notional > limits_.max_order_notional) {
            std::ostringstream ss;
            ss << "Order notional " << notional
               << " exceeds max_order_notional " << limits_.max_order_notional;
            return RiskCheckResult::fail(ss.str());
        }
    } else {
        // Market order: use best available price from map
        auto it = prices.find(order.symbol_id);
        if (it != prices.end()) {
            const double notional = it->second * static_cast<double>(order.qty);
            if (notional > limits_.max_order_notional) {
                std::ostringstream ss;
                ss << "Market order estimated notional " << notional
                   << " exceeds " << limits_.max_order_notional;
                return RiskCheckResult::fail(ss.str());
            }
        }
    }
    return RiskCheckResult::pass();
}

RiskCheckResult RiskEngine::check_position_limit(
        const Order& order, const Portfolio& portfolio) const noexcept {
    const auto* pos = portfolio.position(order.symbol_id);
    const int64_t existing_qty = pos ? pos->qty() : 0;

    int64_t post_fill_qty;
    if (order.is_buy())
        post_fill_qty = existing_qty + static_cast<int64_t>(order.qty);
    else
        post_fill_qty = existing_qty - static_cast<int64_t>(order.qty);

    if (std::abs(post_fill_qty) > limits_.max_position_qty) {
        std::ostringstream ss;
        ss << "Post-fill position " << post_fill_qty
           << " for symbol " << order.symbol_id
           << " exceeds max_position_qty " << limits_.max_position_qty;
        return RiskCheckResult::fail(ss.str());
    }
    return RiskCheckResult::pass();
}

RiskCheckResult RiskEngine::check_exposure(const Order&     order,
                                             const Portfolio& portfolio,
                                             const PriceMap&  prices) const noexcept {
    // Estimate order notional
    double order_notional = 0.0;
    if (order.limit_price > 0) {
        order_notional = md::from_price(order.limit_price)
                       * static_cast<double>(order.qty);
    } else {
        auto it = prices.find(order.symbol_id);
        if (it != prices.end())
            order_notional = it->second * static_cast<double>(order.qty);
    }

    // Gross exposure: always increases by |order_notional|
    const double post_gross = portfolio.gross_exposure(prices) + order_notional;
    if (post_gross > limits_.max_gross_exposure) {
        std::ostringstream ss;
        ss << "Post-order gross exposure " << post_gross
           << " exceeds max " << limits_.max_gross_exposure;
        return RiskCheckResult::fail(ss.str());
    }

    // Net exposure
    const double signed_notional = order.is_buy() ? order_notional : -order_notional;
    const double post_net = portfolio.net_exposure(prices) + signed_notional;
    if (std::abs(post_net) > limits_.max_net_exposure) {
        std::ostringstream ss;
        ss << "Post-order |net exposure| " << std::abs(post_net)
           << " exceeds max " << limits_.max_net_exposure;
        return RiskCheckResult::fail(ss.str());
    }

    return RiskCheckResult::pass();
}

RiskCheckResult RiskEngine::check_daily_loss() const noexcept {
    if (daily_pnl_ < -limits_.max_daily_loss) {
        std::ostringstream ss;
        ss << "Daily P&L " << daily_pnl_
           << " breaches daily loss limit -" << limits_.max_daily_loss;
        return RiskCheckResult::fail(ss.str());
    }
    return RiskCheckResult::pass();
}

RiskCheckResult RiskEngine::check_drawdown() const noexcept {
    if (drawdown_ > limits_.max_drawdown_pct) {
        std::ostringstream ss;
        ss << "Current drawdown " << drawdown_ * 100.0
           << "% exceeds limit " << limits_.max_drawdown_pct * 100.0 << "%";
        return RiskCheckResult::fail(ss.str());
    }
    return RiskCheckResult::pass();
}

} // namespace risk
