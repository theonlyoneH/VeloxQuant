#pragma once
// strategy/momentum.hpp  ── Rate-of-Change momentum strategy
//
// Computes the price Rate-of-Change (ROC) over a lookback window:
//   ROC = (close_now - close_N_bars_ago) / close_N_bars_ago
//
// Signal logic:
//   ROC >  threshold → Long  (strong upward momentum)
//   ROC < -threshold → Short (strong downward momentum)
//   else             → Flat
//
// Optional EMA smoothing: applies an exponential moving average
// to the raw ROC series before thresholding.
//
// Signal strength = min(|ROC| / (2 × threshold), 1.0)

#include "strategy/strategy_base.hpp"

namespace strat {

// ── MomentumStrategy ─────────────────────────────────────────────────────────
class MomentumStrategy : public StrategyBase<MomentumStrategy> {
public:
    struct Params {
        SymbolId    symbol_id;
        std::size_t lookback   {20};      ///< ROC look-back period (bars)
        double      threshold  {0.02};    ///< ROC threshold (e.g. 2%)
        double      ema_alpha  {0.0};     ///< EMA smoothing factor; 0 = disabled
    };

    explicit MomentumStrategy(Params p);

    void on_bar(const Bar& bar) override;

    [[nodiscard]] const char* name() const noexcept override { return "Momentum"; }

    void reset_derived();

    // ── Accessors ─────────────────────────────────────────────────────────────
    [[nodiscard]] double raw_roc()      const noexcept { return raw_roc_;      }
    [[nodiscard]] double smoothed_roc() const noexcept { return smoothed_roc_; }
    [[nodiscard]] bool   is_warmed_up() const noexcept;

private:
    Params params_;

    std::deque<double> close_history_;  ///< Stores lookback+1 prices
    double raw_roc_      {0.0};
    double smoothed_roc_ {0.0};
    bool   ema_init_     {false};
};

} // namespace strat
