#pragma once
// risk/risk_limits.hpp  ── Risk limit configuration
//
// All monetary values are in the same units as Portfolio cash (e.g. USD).
// All percentage values are fractions: 0.10 = 10%.

#include <cstdint>

namespace risk {

struct RiskLimits {
    // ── Position limits ───────────────────────────────────────────────────────
    int64_t  max_position_qty      {1'000'000};  ///< Max |qty| per symbol
    double   max_gross_exposure    {5'000'000.0};///< Sum |notional| across all symbols
    double   max_net_exposure      {2'000'000.0};///< Abs(sum signed notional) 

    // ── Order limits ──────────────────────────────────────────────────────────
    uint64_t max_order_qty         {100'000};    ///< Max qty per single order
    double   max_order_notional    {1'000'000.0};///< Max notional per order

    // ── Drawdown / loss limits ────────────────────────────────────────────────
    double   max_drawdown_pct      {0.10};       ///< 10% max drawdown from HWM
    double   max_daily_loss        {50'000.0};   ///< Max daily loss (absolute)
    double   daily_loss_reset_hrs  {24.0};       ///< Hours before daily loss resets

    // ── VaR limit ─────────────────────────────────────────────────────────────
    double   var_limit_95          {100'000.0};  ///< Max 95% VaR

    // ── Kill-switch ───────────────────────────────────────────────────────────
    bool     kill_switch_enabled   {false};      ///< If true, reject all orders
};

} // namespace risk
