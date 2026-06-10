#pragma once
// exchange/execution_sim.hpp  ── Slippage and commission simulation
//
// Design:
//  • ExecutionSimulator is a pure computation layer with no state dependencies.
//  • Slippage models:
//      Fixed   – constant bps applied to every fill
//      Linear  – impact proportional to (fill_qty / adv_qty)
//      SqrtImpact – Almgren-Chriss √(participation) model
//  • Commission models:
//      PerShare  – rate × qty
//      PerTrade  – flat fee per order
//      Bps       – rate × notional (price × qty)
//  • compute_slippage() walks the book depth to estimate the VWAP fill price
//    versus the arrival price, returning signed slippage (negative = worse).
//  • All results are in fixed-point Price units (× 1e8).

#include "exchange/types.hpp"

#include <cmath>
#include <span>
#include <vector>

namespace exch {

// ── Slippage model selector ───────────────────────────────────────────────────
enum class SlippageModel : uint8_t {
    None        = 0,   ///< No slippage (ideal execution)
    Fixed       = 1,   ///< Constant bps regardless of size
    Linear      = 2,   ///< Impact ∝ qty / adv_qty
    SqrtImpact  = 3,   ///< Impact ∝ σ × √(qty / adv_qty)  (Almgren-Chriss)
    BookWalk    = 4,   ///< Walk the limit book; actual VWAP vs arrival price
};

// ── Commission model selector ─────────────────────────────────────────────────
enum class CommissionModel : uint8_t {
    None     = 0,
    PerShare = 1,   ///< commission = rate × qty  (rate in price units / share)
    PerTrade = 2,   ///< commission = flat_fee   (price units per trade)
    Bps      = 3,   ///< commission = bps_rate × notional / 10000
};

// ── Configuration ─────────────────────────────────────────────────────────────
struct ExecutionSimConfig {
    // Slippage
    SlippageModel slippage_model {SlippageModel::BookWalk};
    double        fixed_slippage_bps{0.5};  ///< For Fixed model
    double        linear_impact_bps {1.0};  ///< For Linear: bps per unit ADV
    double        sigma             {0.01}; ///< Volatility (for SqrtImpact)
    double        adv_qty           {1e6};  ///< Average daily volume (shares)

    // Commission
    CommissionModel commission_model{CommissionModel::PerShare};
    double          per_share_rate  {0.005};   ///< USD per share (default: half-cent)
    double          per_trade_flat  {1.00};    ///< USD flat fee per trade
    double          bps_rate        {10.0};    ///< Basis points commission
};

// ── ExecutionSimulator ────────────────────────────────────────────────────────
class ExecutionSimulator {
public:
    explicit ExecutionSimulator(ExecutionSimConfig cfg = {}) noexcept;

    // ── Slippage ─────────────────────────────────────────────────────────────

    /// Walk the book depth and compute VWAP fill price vs arrival price.
    /// Returns signed slippage: positive = slippage cost, negative = improvement.
    /// `depth` must be the contra-side depth (asks for buys, bids for sells).
    /// `arrival_price` is the best quote at time of submission.
    [[nodiscard]] Price compute_slippage(
        Side side,
        Quantity fill_qty,
        Price arrival_price,
        std::span<const PriceLevel> depth) const noexcept;

    /// Model-based slippage (no book walk needed).
    [[nodiscard]] Price model_slippage(
        Side side,
        Quantity fill_qty,
        Price arrival_price) const noexcept;

    // ── Commission ────────────────────────────────────────────────────────────

    /// Compute commission for a fill.
    [[nodiscard]] Price compute_commission(
        Quantity fill_qty,
        Price fill_price) const noexcept;

    // ── Annotate a fill ───────────────────────────────────────────────────────

    /// Annotate a Fill's slippage and commission fields in place.
    /// `depth`         = contra-side depth snapshot at submission time.
    /// `arrival_price` = best quote at order submission.
    void annotate(Fill& fill,
                  Price arrival_price,
                  std::span<const PriceLevel> depth) const noexcept;

    // ── Config access ─────────────────────────────────────────────────────────
    [[nodiscard]] const ExecutionSimConfig& config() const noexcept { return cfg_; }
    void set_config(ExecutionSimConfig cfg) noexcept { cfg_ = cfg; }

private:
    ExecutionSimConfig cfg_;

    [[nodiscard]] Price book_walk_slippage(
        Side side, Quantity qty, Price arrival,
        std::span<const PriceLevel> depth) const noexcept;

    [[nodiscard]] Price fixed_slippage(
        Side side, Price arrival) const noexcept;

    [[nodiscard]] Price linear_slippage(
        Side side, Quantity qty, Price arrival) const noexcept;

    [[nodiscard]] Price sqrt_impact_slippage(
        Side side, Quantity qty, Price arrival) const noexcept;
};

} // namespace exch
