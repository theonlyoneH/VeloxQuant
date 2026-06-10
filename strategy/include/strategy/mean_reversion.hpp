#pragma once
// strategy/mean_reversion.hpp  ── Z-score mean reversion strategy
//
// Uses a rolling mean + rolling stddev (computed via Welford's online algorithm)
// over a lookback window of bar closes.
//
// Signal logic:
//   z = (price - mean) / stddev
//   z >  entry_z  → Short  (price above band → expect reversion down)
//   z < -entry_z  → Long   (price below band → expect reversion up)
//   |z| < exit_z  → Flat   (price near mean → take profit / stay flat)
//
// entry_z must be > exit_z to avoid signal chatter near the mean.

#include "strategy/strategy_base.hpp"

namespace strat {

// ── MeanReversionStrategy ────────────────────────────────────────────────────
class MeanReversionStrategy : public StrategyBase<MeanReversionStrategy> {
public:
    struct Params {
        SymbolId    symbol_id;
        std::size_t lookback   {20};   ///< Rolling window length (bars)
        double      entry_z    {2.0};  ///< Enter position when |z| > entry_z
        double      exit_z     {0.5};  ///< Flatten when |z| < exit_z
    };

    explicit MeanReversionStrategy(Params p);

    void on_bar(const Bar& bar) override;

    [[nodiscard]] const char* name() const noexcept override {
        return "MeanReversion";
    }

    void reset_derived();

    // ── Accessors ─────────────────────────────────────────────────────────────
    [[nodiscard]] double rolling_mean()   const noexcept { return mean_;   }
    [[nodiscard]] double rolling_stddev() const noexcept { return stddev_; }
    [[nodiscard]] double z_score()        const noexcept { return z_;      }
    [[nodiscard]] bool   is_warmed_up()   const noexcept;

private:
    Params params_;

    // Rolling window of prices for mean/variance
    std::deque<double> window_;
    double sum_  {0.0};
    double sum2_ {0.0};   ///< Sum of squares for variance

    double mean_  {0.0};
    double stddev_{0.0};
    double z_     {0.0};

    void update_stats(double price);
};

} // namespace strat
