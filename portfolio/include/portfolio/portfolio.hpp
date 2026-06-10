#pragma once
// portfolio/portfolio.hpp  ── Multi-symbol portfolio with NAV and equity curve
//
// Design:
//  • Portfolio owns a map of Position objects and a cash balance.
//  • on_fill() routes fills to the correct Position and deducts/adds cash.
//    Cash accounting: Buy  costs  fill_price×qty + commission (cash outflow)
//                     Sell receives fill_price×qty - commission (cash inflow)
//  • nav() computes Net Asset Value = cash + sum(gross_exposure × sign(qty))
//    using a caller-supplied current price map.
//  • snapshot_equity() appends a {timestamp, nav} point to the equity curve.
//  • The portfolio is NOT thread-safe.

#include "portfolio/position.hpp"

#include <functional>
#include <unordered_map>
#include <vector>

namespace port {

// ── EquityPoint ───────────────────────────────────────────────────────────────
struct EquityPoint {
    Timestamp timestamp;
    double    nav;       ///< Net Asset Value at this instant
};

using EquityCurve = std::vector<EquityPoint>;

// ── PriceMap ─────────────────────────────────────────────────────────────────
using PriceMap = std::unordered_map<SymbolId, double>;

// ── Portfolio ─────────────────────────────────────────────────────────────────
class Portfolio {
public:
    explicit Portfolio(double initial_cash = 1'000'000.0) noexcept;

    // ── Update ────────────────────────────────────────────────────────────────

    /// Process a fill: update the relevant position and cash balance.
    void on_fill(const Fill& fill);

    /// Take a NAV snapshot and append to equity_curve.
    void snapshot_equity(Timestamp ts, const PriceMap& prices);

    // ── Queries ───────────────────────────────────────────────────────────────

    [[nodiscard]] double cash() const noexcept { return cash_; }

    [[nodiscard]] double nav(const PriceMap& prices) const noexcept;

    [[nodiscard]] double realized_pnl() const noexcept;

    [[nodiscard]] double unrealized_pnl(const PriceMap& prices) const noexcept;

    [[nodiscard]] double total_pnl(const PriceMap& prices) const noexcept {
        return realized_pnl() + unrealized_pnl(prices);
    }

    /// Gross exposure: sum of |qty| × price for all positions.
    [[nodiscard]] double gross_exposure(const PriceMap& prices) const noexcept;

    /// Net exposure: sum of qty × price (signed).
    [[nodiscard]] double net_exposure(const PriceMap& prices) const noexcept;

    /// Look up a position (nullptr if symbol not held).
    [[nodiscard]] const Position* position(SymbolId id) const noexcept;

    [[nodiscard]] const std::unordered_map<SymbolId, Position>&
    positions() const noexcept { return positions_; }

    [[nodiscard]] const EquityCurve& equity_curve() const noexcept {
        return equity_curve_;
    }

    [[nodiscard]] uint64_t fill_count()   const noexcept { return fill_count_;   }
    [[nodiscard]] double   initial_cash() const noexcept { return initial_cash_; }

    /// Total return vs. initial cash: (nav - initial_cash) / initial_cash
    [[nodiscard]] double total_return(const PriceMap& prices) const noexcept {
        return (nav(prices) - initial_cash_) / initial_cash_;
    }

private:
    double initial_cash_;
    double cash_;
    std::unordered_map<SymbolId, Position> positions_;
    EquityCurve equity_curve_;
    uint64_t fill_count_{0};
};

} // namespace port
