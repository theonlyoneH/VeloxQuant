// portfolio/position.cpp  ── Position accounting implementation

#include "portfolio/position.hpp"
#include "market_data/types.hpp"

#include <cassert>
#include <cmath>
#include <stdexcept>

namespace port {

// ── on_fill ───────────────────────────────────────────────────────────────────
void Position::on_fill(const Fill& fill) {
    ++fill_count_;

    const double fill_price = md::from_price(fill.fill_price);
    const int64_t fill_qty  = static_cast<int64_t>(fill.fill_qty);

    // Direction: buy = +, sell = -
    const int64_t delta_qty =
        (fill.side == Side::Buy) ? fill_qty : -fill_qty;

    const bool is_opening =
        (qty_ == 0) ||
        (qty_ > 0 && delta_qty > 0) ||   // adding to long
        (qty_ < 0 && delta_qty < 0);      // adding to short

    const bool is_closing =
        (qty_ > 0 && delta_qty < 0) ||   // selling from long
        (qty_ < 0 && delta_qty > 0);      // covering short

    if (is_opening) {
        open_or_add(fill_price, delta_qty);
        return;
    }

    if (is_closing) {
        const int64_t close_qty = std::min(std::abs(delta_qty), std::abs(qty_));
        reduce(fill_price, close_qty);

        // If the fill reverses the position (fill_qty > existing qty)
        const int64_t remainder = delta_qty + qty_;
        if (remainder != 0) {
            // Fully closed out, now opening in new direction
            open_or_add(fill_price, remainder);
        }
    }
}

// ── open_or_add ───────────────────────────────────────────────────────────────
void Position::open_or_add(double fill_price, int64_t delta_qty) {
    if (qty_ == 0) {
        avg_cost_ = fill_price;
        qty_      = delta_qty;
    } else {
        // Weighted average cost
        const double old_notional = avg_cost_ * static_cast<double>(std::abs(qty_));
        const double new_notional = fill_price * static_cast<double>(std::abs(delta_qty));
        const int64_t new_qty = qty_ + delta_qty;
        avg_cost_ = (old_notional + new_notional) / static_cast<double>(std::abs(new_qty));
        qty_      = new_qty;
    }
}

// ── reduce ────────────────────────────────────────────────────────────────────
void Position::reduce(double fill_price, int64_t close_qty) {
    // P&L = (fill_price - avg_cost) × qty for longs
    //     = (avg_cost - fill_price) × qty for shorts
    const double pnl_per_share =
        (qty_ > 0)
            ? (fill_price - avg_cost_)
            : (avg_cost_ - fill_price);

    realized_pnl_ += pnl_per_share * static_cast<double>(close_qty);

    // Reduce position
    if (qty_ > 0)
        qty_ -= close_qty;
    else
        qty_ += close_qty;

    // If flat, reset avg_cost
    if (qty_ == 0) avg_cost_ = 0.0;
}

// ── unrealized_pnl ────────────────────────────────────────────────────────────
double Position::unrealized_pnl(double current_price) const noexcept {
    if (qty_ == 0) return 0.0;
    const double pnl_per_share =
        (qty_ > 0)
            ? (current_price - avg_cost_)
            : (avg_cost_ - current_price);
    return pnl_per_share * static_cast<double>(std::abs(qty_));
}

} // namespace port
