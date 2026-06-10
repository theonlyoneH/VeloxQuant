// strategy/momentum.cpp  ── MomentumStrategy implementation

#include "strategy/momentum.hpp"
#include "market_data/types.hpp"

#include <cassert>
#include <cmath>

namespace strat {

MomentumStrategy::MomentumStrategy(Params p)
    : StrategyBase<MomentumStrategy>(p.symbol_id, p.lookback + 1)
    , params_(p)
{
    assert(p.lookback >= 1);
    assert(p.threshold > 0.0);
}

// ── on_bar ────────────────────────────────────────────────────────────────────
void MomentumStrategy::on_bar(const Bar& bar) {
    if (bar.symbol_id != params_.symbol_id) return;

    const double close = md::from_price(bar.close);

    close_history_.push_back(close);
    // Keep exactly lookback+1 prices so we can compute the full ROC window
    if (close_history_.size() > params_.lookback + 1)
        close_history_.pop_front();

    if (!is_warmed_up()) return;

    const double price_n_ago = close_history_.front();
    if (price_n_ago == 0.0) return;  // guard division by zero

    // Raw ROC
    raw_roc_ = (close - price_n_ago) / price_n_ago;

    // Optional EMA smoothing
    if (params_.ema_alpha > 0.0) {
        if (!ema_init_) {
            smoothed_roc_ = raw_roc_;
            ema_init_     = true;
        } else {
            smoothed_roc_ = params_.ema_alpha * raw_roc_
                          + (1.0 - params_.ema_alpha) * smoothed_roc_;
        }
    } else {
        smoothed_roc_ = raw_roc_;
    }

    // Classify signal
    SignalType sig = SignalType::Flat;
    double strength = 0.0;

    if (smoothed_roc_ > params_.threshold) {
        sig      = SignalType::Long;
        // Strength scales with how far ROC exceeds threshold, capped at 1
        strength = std::min(smoothed_roc_ / (2.0 * params_.threshold), 1.0);
    } else if (smoothed_roc_ < -params_.threshold) {
        sig      = SignalType::Short;
        strength = std::min(-smoothed_roc_ / (2.0 * params_.threshold), 1.0);
    }

    emit(sig, strength, bar.close_ts);
}

// ── reset_derived ─────────────────────────────────────────────────────────────
void MomentumStrategy::reset_derived() {
    close_history_.clear();
    raw_roc_      = 0.0;
    smoothed_roc_ = 0.0;
    ema_init_     = false;
}

// ── is_warmed_up ──────────────────────────────────────────────────────────────
bool MomentumStrategy::is_warmed_up() const noexcept {
    return close_history_.size() >= params_.lookback + 1;
}

} // namespace strat
