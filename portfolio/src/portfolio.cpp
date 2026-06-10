// portfolio/portfolio.cpp  ── Portfolio implementation

#include "portfolio/portfolio.hpp"
#include "market_data/types.hpp"

#include <numeric>

namespace port {

// ── Construction ──────────────────────────────────────────────────────────────
Portfolio::Portfolio(double initial_cash) noexcept
    : initial_cash_(initial_cash), cash_(initial_cash)
{}

// ── on_fill ───────────────────────────────────────────────────────────────────
void Portfolio::on_fill(const Fill& fill) {
    ++fill_count_;

    // Route to position
    auto [it, inserted] = positions_.try_emplace(fill.symbol_id,
                                                  fill.symbol_id);
    it->second.on_fill(fill);

    // Cash accounting
    const double notional   = md::from_price(fill.fill_price)
                            * static_cast<double>(fill.fill_qty);
    const double commission = md::from_price(fill.commission);

    if (fill.side == Side::Buy) {
        cash_ -= (notional + commission);  // outflow
    } else {
        cash_ += (notional - commission);  // inflow
    }
}

// ── snapshot_equity ───────────────────────────────────────────────────────────
void Portfolio::snapshot_equity(Timestamp ts, const PriceMap& prices) {
    equity_curve_.push_back({ts, nav(prices)});
}

// ── nav ───────────────────────────────────────────────────────────────────────
double Portfolio::nav(const PriceMap& prices) const noexcept {
    double total = cash_;
    for (const auto& [sym, pos] : positions_) {
        if (pos.is_flat()) continue;
        auto pit = prices.find(sym);
        if (pit == prices.end()) continue;
        // Long: +qty × price; Short: +qty × price  (qty is signed)
        total += static_cast<double>(pos.qty()) * pit->second;
    }
    return total;
}

// ── realized_pnl ─────────────────────────────────────────────────────────────
double Portfolio::realized_pnl() const noexcept {
    double total = 0.0;
    for (const auto& [sym, pos] : positions_)
        total += pos.realized_pnl();
    return total;
}

// ── unrealized_pnl ───────────────────────────────────────────────────────────
double Portfolio::unrealized_pnl(const PriceMap& prices) const noexcept {
    double total = 0.0;
    for (const auto& [sym, pos] : positions_) {
        auto pit = prices.find(sym);
        if (pit == prices.end()) continue;
        total += pos.unrealized_pnl(pit->second);
    }
    return total;
}

// ── gross_exposure ────────────────────────────────────────────────────────────
double Portfolio::gross_exposure(const PriceMap& prices) const noexcept {
    double total = 0.0;
    for (const auto& [sym, pos] : positions_) {
        if (pos.is_flat()) continue;
        auto pit = prices.find(sym);
        if (pit == prices.end()) continue;
        total += pos.gross_exposure(pit->second);
    }
    return total;
}

// ── net_exposure ──────────────────────────────────────────────────────────────
double Portfolio::net_exposure(const PriceMap& prices) const noexcept {
    double total = 0.0;
    for (const auto& [sym, pos] : positions_) {
        if (pos.is_flat()) continue;
        auto pit = prices.find(sym);
        if (pit == prices.end()) continue;
        total += static_cast<double>(pos.qty()) * pit->second;
    }
    return total;
}

// ── position ─────────────────────────────────────────────────────────────────
const Position* Portfolio::position(SymbolId id) const noexcept {
    auto it = positions_.find(id);
    return (it != positions_.end()) ? &it->second : nullptr;
}

} // namespace port
