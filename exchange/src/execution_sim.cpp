// exchange/execution_sim.cpp  ── Slippage and commission models

#include "exchange/execution_sim.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace exch {

// ── Construction ──────────────────────────────────────────────────────────────
ExecutionSimulator::ExecutionSimulator(ExecutionSimConfig cfg) noexcept
    : cfg_(cfg)
{}

// ── compute_slippage ─────────────────────────────────────────────────────────
Price ExecutionSimulator::compute_slippage(Side side, Quantity fill_qty,
                                            Price arrival_price,
                                            std::span<const PriceLevel> depth) const noexcept {
    switch (cfg_.slippage_model) {
    case SlippageModel::None:
        return 0;
    case SlippageModel::Fixed:
        return fixed_slippage(side, arrival_price);
    case SlippageModel::Linear:
        return linear_slippage(side, fill_qty, arrival_price);
    case SlippageModel::SqrtImpact:
        return sqrt_impact_slippage(side, fill_qty, arrival_price);
    case SlippageModel::BookWalk:
        return book_walk_slippage(side, fill_qty, arrival_price, depth);
    }
    return 0;
}

// ── model_slippage (without book depth) ──────────────────────────────────────
Price ExecutionSimulator::model_slippage(Side side, Quantity fill_qty,
                                          Price arrival_price) const noexcept {
    switch (cfg_.slippage_model) {
    case SlippageModel::None:       return 0;
    case SlippageModel::Fixed:      return fixed_slippage(side, arrival_price);
    case SlippageModel::Linear:     return linear_slippage(side, fill_qty, arrival_price);
    case SlippageModel::SqrtImpact: return sqrt_impact_slippage(side, fill_qty, arrival_price);
    case SlippageModel::BookWalk:
        // No depth: fall back to linear
        return linear_slippage(side, fill_qty, arrival_price);
    }
    return 0;
}

// ── book_walk_slippage ────────────────────────────────────────────────────────
// Walk the resting depth, computing VWAP fill price vs. arrival price.
Price ExecutionSimulator::book_walk_slippage(Side side, Quantity qty,
                                              Price arrival,
                                              std::span<const PriceLevel> depth) const noexcept {
    if (arrival == 0 || qty == 0 || depth.empty()) return 0;

    Quantity remaining  = qty;
    int64_t  pv_sum     = 0;   // sum(price × qty) for VWAP

    for (const auto& lvl : depth) {
        if (remaining == 0) break;
        const Quantity take = std::min(remaining, lvl.total_qty);
        pv_sum    += static_cast<int64_t>(lvl.price) * static_cast<int64_t>(take);
        remaining -= take;
    }

    // If book exhausted before filling completely, assume last level continues
    if (remaining > 0 && !depth.empty()) {
        pv_sum += static_cast<int64_t>(depth.back().price)
                * static_cast<int64_t>(remaining);
    }

    const Price vwap = static_cast<Price>(pv_sum / static_cast<int64_t>(qty));

    // Signed slippage: positive means you paid more / received less
    if (side == Side::Buy)
        return vwap - arrival;   // bought above arrival = positive (bad)
    else
        return arrival - vwap;   // sold below arrival = positive (bad)
}

// ── fixed_slippage ────────────────────────────────────────────────────────────
Price ExecutionSimulator::fixed_slippage(Side side, Price arrival) const noexcept {
    if (arrival == 0) return 0;
    // slippage = arrival × (bps / 10000)
    const Price slip = static_cast<Price>(
        static_cast<double>(arrival) * cfg_.fixed_slippage_bps / 10000.0);
    // Buy: slippage is positive (pay more).  Sell: positive (receive less).
    return (side == Side::Buy) ? slip : slip;
}

// ── linear_slippage ───────────────────────────────────────────────────────────
Price ExecutionSimulator::linear_slippage(Side side, Quantity qty,
                                           Price arrival) const noexcept {
    if (arrival == 0 || cfg_.adv_qty <= 0.0) return 0;
    const double participation = static_cast<double>(qty) / cfg_.adv_qty;
    const double bps = cfg_.linear_impact_bps * participation;
    const Price slip = static_cast<Price>(
        static_cast<double>(arrival) * bps / 10000.0);
    return slip;
}

// ── sqrt_impact_slippage ──────────────────────────────────────────────────────
// Almgren-Chriss: impact ≈ σ × √(qty / adv_qty)
Price ExecutionSimulator::sqrt_impact_slippage(Side side, Quantity qty,
                                                Price arrival) const noexcept {
    if (arrival == 0 || cfg_.adv_qty <= 0.0 || cfg_.sigma <= 0.0) return 0;
    const double participation = static_cast<double>(qty) / cfg_.adv_qty;
    const double impact_frac   = cfg_.sigma * std::sqrt(participation);
    const Price slip = static_cast<Price>(
        static_cast<double>(arrival) * impact_frac);
    return slip;
}

// ── compute_commission ────────────────────────────────────────────────────────
Price ExecutionSimulator::compute_commission(Quantity fill_qty,
                                              Price fill_price) const noexcept {
    switch (cfg_.commission_model) {
    case CommissionModel::None:
        return 0;

    case CommissionModel::PerShare: {
        // rate in price units per share (e.g. $0.005 → to_price(0.005))
        const Price rate = md::to_price(cfg_.per_share_rate);
        return rate * static_cast<int64_t>(fill_qty);
    }

    case CommissionModel::PerTrade: {
        return md::to_price(cfg_.per_trade_flat);
    }

    case CommissionModel::Bps: {
        // commission = bps_rate × notional / 10000
        // notional = fill_price × fill_qty  (fixed-point × uint64 = large int)
        const double notional =
            md::from_price(fill_price) * static_cast<double>(fill_qty);
        const double comm = notional * cfg_.bps_rate / 10000.0;
        return md::to_price(comm);
    }
    }
    return 0;
}

// ── annotate ─────────────────────────────────────────────────────────────────
void ExecutionSimulator::annotate(Fill& fill, Price arrival_price,
                                   std::span<const PriceLevel> depth) const noexcept {
    fill.slippage   = compute_slippage(fill.side, fill.fill_qty,
                                       arrival_price, depth);
    fill.commission = compute_commission(fill.fill_qty, fill.fill_price);
}

} // namespace exch
