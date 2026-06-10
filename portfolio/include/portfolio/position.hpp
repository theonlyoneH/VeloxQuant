#pragma once
// portfolio/position.hpp  ── Single-symbol position accounting
//
// Design:
//  • qty is signed: positive = long, negative = short.
//  • avg_cost is a double (not fixed-point) so that average-cost arithmetic
//    does not accumulate rounding error over many partial fills.
//  • realized_pnl accumulates when position is reduced or reversed.
//  • on_fill() implements correct average-cost basis for longs and shorts:
//      – Opening/adding: update avg_cost via weighted average.
//      – Reducing:       realize P&L on exited portion; avg_cost unchanged.
//      – Reversing:      fully realize existing P&L, then open fresh position.

#include "exchange/types.hpp"
#include "market_data/types.hpp"

#include <cmath>

namespace port {

using md::SymbolId;
using md::Timestamp;
using md::from_price;
using exch::Fill;
using exch::Side;

// ── Position ──────────────────────────────────────────────────────────────────
class Position {
public:
    explicit Position(SymbolId sym) noexcept : symbol_id_(sym) {}

    // ── Update ────────────────────────────────────────────────────────────────

    /// Process a fill and update position/cost/P&L.
    void on_fill(const Fill& fill);

    // ── Queries ───────────────────────────────────────────────────────────────

    [[nodiscard]] SymbolId symbol_id()   const noexcept { return symbol_id_; }
    [[nodiscard]] int64_t  qty()         const noexcept { return qty_; }
    [[nodiscard]] double   avg_cost()    const noexcept { return avg_cost_; }
    [[nodiscard]] double   realized_pnl()const noexcept { return realized_pnl_; }

    [[nodiscard]] bool is_long()  const noexcept { return qty_ > 0; }
    [[nodiscard]] bool is_short() const noexcept { return qty_ < 0; }
    [[nodiscard]] bool is_flat()  const noexcept { return qty_ == 0; }

    /// Mark-to-market unrealized P&L given current mid price.
    [[nodiscard]] double unrealized_pnl(double current_price) const noexcept;

    /// Total P&L = realized + unrealized.
    [[nodiscard]] double total_pnl(double current_price) const noexcept {
        return realized_pnl_ + unrealized_pnl(current_price);
    }

    /// Gross exposure = |qty| × price
    [[nodiscard]] double gross_exposure(double current_price) const noexcept {
        return std::abs(static_cast<double>(qty_)) * current_price;
    }

    [[nodiscard]] uint64_t fill_count() const noexcept { return fill_count_; }

private:
    SymbolId symbol_id_;
    int64_t  qty_         {0};     ///< Signed: + = long, - = short
    double   avg_cost_    {0.0};   ///< Average entry cost per share
    double   realized_pnl_{0.0};
    uint64_t fill_count_  {0};

    void open_or_add(double fill_price, int64_t delta_qty);
    void reduce(double fill_price, int64_t close_qty);
};

} // namespace port
