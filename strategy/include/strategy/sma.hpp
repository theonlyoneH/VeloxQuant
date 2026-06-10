#pragma once
// strategy/sma.hpp  ── Simple Moving Average crossover strategy
//
// Generates signals based on two SMAs (fast and slow).
// Signal:
//   Long  when fast_sma crosses above slow_sma (golden cross)
//   Short when fast_sma crosses below slow_sma (death cross)
//   Flat  during warmup or when no cross has occurred
//
// Implementation uses two O(1) rolling sums instead of recomputing
// the full window on every bar.

#include "strategy/strategy_base.hpp"

namespace strat {

// ── SMAStrategy ───────────────────────────────────────────────────────────────
class SMAStrategy : public StrategyBase<SMAStrategy> {
public:
    struct Params {
        SymbolId    symbol_id;
        std::size_t fast_period  {10};   ///< Fast SMA window (bars)
        std::size_t slow_period  {30};   ///< Slow SMA window (bars)
        double      signal_threshold{0.0}; ///< Min % diff for signal (0 = any cross)
    };

    explicit SMAStrategy(Params p);

    void on_bar(const Bar& bar) override;

    [[nodiscard]] const char* name() const noexcept override { return "SMA"; }

    // Derived reset hook (called by StrategyBase::reset)
    void reset_derived();

    // ── Accessors (for testing) ───────────────────────────────────────────────
    [[nodiscard]] double fast_sma() const noexcept;
    [[nodiscard]] double slow_sma() const noexcept;
    [[nodiscard]] bool   is_warmed_up() const noexcept;

private:
    Params params_;

    // Rolling sums for O(1) SMA computation
    std::deque<double> fast_window_;
    std::deque<double> slow_window_;
    double fast_sum_ {0.0};
    double slow_sum_ {0.0};

    double prev_fast_sma_ {0.0};
    double prev_slow_sma_ {0.0};

    void push(double price);
};

} // namespace strat
