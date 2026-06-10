#pragma once
// risk/risk_engine.hpp  ── Pre-trade checks and breach monitoring
//
// Design:
//  • RiskEngine::check_order() is called BEFORE order submission to the exchange.
//    It is synchronous and returns immediately (no state mutation on reject).
//  • RiskEngine::on_fill() is called AFTER each fill to update internal state
//    (daily P&L, drawdown watermark, etc.).
//  • on_breach() callbacks are fired whenever a limit is first crossed.
//  • The engine holds a const reference to the Portfolio for NAV/position queries.

#include "risk/risk_limits.hpp"
#include "portfolio/portfolio.hpp"
#include "exchange/types.hpp"
#include "market_data/types.hpp"

#include <functional>
#include <string>

namespace risk {

using port::Portfolio;
using port::PriceMap;
using exch::Order;
using exch::Fill;
using md::SymbolId;
using md::Timestamp;

// ── RiskCheckResult ───────────────────────────────────────────────────────────
struct RiskCheckResult {
    bool        ok     {true};
    std::string reason {};

    static RiskCheckResult pass() noexcept { return {true, ""}; }
    static RiskCheckResult fail(std::string r) noexcept {
        return {false, std::move(r)};
    }
};

// ── BreachType ───────────────────────────────────────────────────────────────
enum class BreachType : uint8_t {
    PositionLimit    = 0,
    GrossExposure    = 1,
    NetExposure      = 2,
    DrawdownLimit    = 3,
    DailyLossLimit   = 4,
    VaRLimit         = 5,
    KillSwitch       = 6,
    OrderQtyLimit    = 7,
    OrderNotional    = 8,
};

// ── BreachEvent ───────────────────────────────────────────────────────────────
struct BreachEvent {
    BreachType  type;
    std::string detail;
    Timestamp   timestamp {0};
    double      current_value  {0.0};
    double      limit_value    {0.0};
};

using BreachCallback = std::function<void(const BreachEvent&)>;

// ── RiskEngine ────────────────────────────────────────────────────────────────
class RiskEngine {
public:
    explicit RiskEngine(RiskLimits limits = {}) noexcept;

    // ── Configuration ────────────────────────────────────────────────────────
    void set_limits(RiskLimits limits) noexcept { limits_ = limits; }
    [[nodiscard]] const RiskLimits& limits() const noexcept { return limits_; }

    void on_breach(BreachCallback cb) { breach_cb_ = std::move(cb); }

    // ── Pre-trade check ──────────────────────────────────────────────────────
    /// Check an order BEFORE submission.
    /// @param order        The order being submitted.
    /// @param portfolio    Current portfolio state.
    /// @param prices       Current prices (for exposure calculation).
    /// @param now          Simulation timestamp.
    [[nodiscard]]
    RiskCheckResult check_order(const Order&     order,
                                 const Portfolio& portfolio,
                                 const PriceMap&  prices,
                                 Timestamp        now);

    // ── Post-fill update ─────────────────────────────────────────────────────
    /// Update internal risk state after a fill.
    void on_fill(const Fill&     fill,
                 const Portfolio& portfolio,
                 const PriceMap&  prices,
                 Timestamp        now);

    // ── Manual state queries ─────────────────────────────────────────────────
    [[nodiscard]] double daily_pnl()       const noexcept { return daily_pnl_; }
    [[nodiscard]] double high_water_mark() const noexcept { return hwm_;       }
    [[nodiscard]] double current_drawdown()const noexcept { return drawdown_;  }
    [[nodiscard]] bool   kill_switch()     const noexcept {
        return limits_.kill_switch_enabled;
    }

    /// Force the kill switch on
    void engage_kill_switch() noexcept { limits_.kill_switch_enabled = true; }

    /// Reset daily P&L counter (call at start of each new session)
    void reset_daily_pnl(Timestamp session_start_ts) noexcept;

    /// Set the high-water mark (e.g. at start of backtest)
    void set_hwm(double nav) noexcept { hwm_ = nav; }

private:
    RiskLimits     limits_;
    BreachCallback breach_cb_;

    double daily_pnl_      {0.0};
    double prev_nav_       {0.0};  ///< NAV at last fill
    double hwm_            {0.0};  ///< High-water mark for drawdown
    double drawdown_       {0.0};  ///< Current peak-to-trough fraction
    Timestamp session_start_ts_{0};

    void fire_breach(BreachEvent ev);

    // ── Individual check helpers ──────────────────────────────────────────────
    [[nodiscard]] RiskCheckResult check_kill_switch() const noexcept;
    [[nodiscard]] RiskCheckResult check_order_qty(const Order& order) const noexcept;
    [[nodiscard]] RiskCheckResult check_order_notional(const Order& order,
                                                        const PriceMap& prices) const noexcept;
    [[nodiscard]] RiskCheckResult check_position_limit(const Order& order,
                                                        const Portfolio& portfolio) const noexcept;
    [[nodiscard]] RiskCheckResult check_exposure(const Order& order,
                                                  const Portfolio& portfolio,
                                                  const PriceMap& prices) const noexcept;
    [[nodiscard]] RiskCheckResult check_daily_loss() const noexcept;
    [[nodiscard]] RiskCheckResult check_drawdown() const noexcept;
};

} // namespace risk
