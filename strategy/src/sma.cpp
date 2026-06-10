// strategy/sma.cpp  ── SMAStrategy implementation

#include "strategy/sma.hpp"
#include "market_data/types.hpp"

#include <cassert>
#include <cmath>

namespace strat {

SMAStrategy::SMAStrategy(Params p)
    : StrategyBase<SMAStrategy>(p.symbol_id,
                                 p.slow_period + 1)
    , params_(p)
{
    assert(p.fast_period >= 2 && "fast_period must be >= 2");
    assert(p.slow_period > p.fast_period && "slow_period must be > fast_period");
}

// ── push ─────────────────────────────────────────────────────────────────────
void SMAStrategy::push(double price) {
    // Fast window
    fast_window_.push_back(price);
    fast_sum_ += price;
    if (fast_window_.size() > params_.fast_period) {
        fast_sum_ -= fast_window_.front();
        fast_window_.pop_front();
    }

    // Slow window
    slow_window_.push_back(price);
    slow_sum_ += price;
    if (slow_window_.size() > params_.slow_period) {
        slow_sum_ -= slow_window_.front();
        slow_window_.pop_front();
    }
}

// ── on_bar ────────────────────────────────────────────────────────────────────
void SMAStrategy::on_bar(const Bar& bar) {
    if (bar.symbol_id != params_.symbol_id) return;

    const double close = md::from_price(bar.close);
    push(close);

    if (!is_warmed_up()) {
        prev_fast_sma_ = fast_sma();
        prev_slow_sma_ = slow_sma();
        return;
    }

    const double cur_fast = fast_sma();
    const double cur_slow = slow_sma();

    // Detect crossover
    const bool was_above = prev_fast_sma_ > prev_slow_sma_;
    const bool is_above  = cur_fast > cur_slow;

    SignalType  sig_type = SignalType::Flat;
    double      strength = 0.0;

    if (!was_above && is_above) {
        // Golden cross → Long
        sig_type = SignalType::Long;
        strength = std::min((cur_fast - cur_slow) / cur_slow, 1.0);
    } else if (was_above && !is_above) {
        // Death cross → Short
        sig_type = SignalType::Short;
        strength = std::min((cur_slow - cur_fast) / cur_slow, 1.0);
    } else {
        // Sustain current direction as flat (no new cross)
        sig_type = SignalType::Flat;
    }

    prev_fast_sma_ = cur_fast;
    prev_slow_sma_ = cur_slow;

    // Apply optional threshold filter
    if (sig_type != SignalType::Flat) {
        const double pct_diff = std::abs(cur_fast - cur_slow) / cur_slow;
        if (pct_diff < params_.signal_threshold) {
            sig_type = SignalType::Flat;
            strength = 0.0;
        }
    }

    emit(sig_type, strength, bar.close_ts);
}

// ── reset_derived ─────────────────────────────────────────────────────────────
void SMAStrategy::reset_derived() {
    fast_window_.clear();
    slow_window_.clear();
    fast_sum_      = 0.0;
    slow_sum_      = 0.0;
    prev_fast_sma_ = 0.0;
    prev_slow_sma_ = 0.0;
}

// ── Accessors ─────────────────────────────────────────────────────────────────
double SMAStrategy::fast_sma() const noexcept {
    if (fast_window_.empty()) return 0.0;
    return fast_sum_ / static_cast<double>(fast_window_.size());
}

double SMAStrategy::slow_sma() const noexcept {
    if (slow_window_.empty()) return 0.0;
    return slow_sum_ / static_cast<double>(slow_window_.size());
}

bool SMAStrategy::is_warmed_up() const noexcept {
    return fast_window_.size() >= params_.fast_period
        && slow_window_.size() >= params_.slow_period;
}

} // namespace strat
