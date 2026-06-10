// strategy/mean_reversion.cpp  ── MeanReversionStrategy implementation

#include "strategy/mean_reversion.hpp"
#include "market_data/types.hpp"

#include <cassert>
#include <cmath>

namespace strat {

MeanReversionStrategy::MeanReversionStrategy(Params p)
    : StrategyBase<MeanReversionStrategy>(p.symbol_id, p.lookback)
    , params_(p)
{
    assert(p.lookback >= 2);
    assert(p.entry_z > p.exit_z && "entry_z must be > exit_z");
}

// ── update_stats ──────────────────────────────────────────────────────────────
// Maintains a rolling window of size `lookback` using a sum-of-squares trick
// for variance.  This is numerically stable for reasonably scaled price inputs
// (avoid catastrophic cancellation at extreme precision).
void MeanReversionStrategy::update_stats(double price) {
    // Add new sample
    window_.push_back(price);
    sum_  += price;
    sum2_ += price * price;

    // Evict oldest if window full
    if (window_.size() > params_.lookback) {
        const double old = window_.front();
        window_.pop_front();
        sum_  -= old;
        sum2_ -= old * old;
    }

    const std::size_t n = window_.size();
    if (n < 2) {
        mean_   = (n == 1) ? price : 0.0;
        stddev_ = 0.0;
        z_      = 0.0;
        return;
    }

    mean_ = sum_ / static_cast<double>(n);
    // Population variance of the window (biased): E[X²] - (E[X])²
    const double variance = (sum2_ / static_cast<double>(n)) - (mean_ * mean_);
    stddev_ = std::sqrt(std::max(variance, 0.0));  // guard floating-point noise

    z_ = (stddev_ > 0.0)
        ? (price - mean_) / stddev_
        : 0.0;
}

// ── on_bar ────────────────────────────────────────────────────────────────────
void MeanReversionStrategy::on_bar(const Bar& bar) {
    if (bar.symbol_id != params_.symbol_id) return;

    const double close = md::from_price(bar.close);
    update_stats(close);

    if (!is_warmed_up()) return;

    SignalType sig  = SignalType::Flat;
    double     strength = 0.0;

    if (z_ > params_.entry_z) {
        sig      = SignalType::Short;
        strength = std::min((z_ - params_.entry_z) / params_.entry_z, 1.0);
    } else if (z_ < -params_.entry_z) {
        sig      = SignalType::Long;
        strength = std::min((-z_ - params_.entry_z) / params_.entry_z, 1.0);
    } else if (std::abs(z_) < params_.exit_z) {
        sig = SignalType::Flat;
    } else {
        // Between exit_z and entry_z → sustain last direction but do not emit new
        // (no-op: only emit on clear entry or exit)
        return;
    }

    emit(sig, strength, bar.close_ts);
}

// ── reset_derived ─────────────────────────────────────────────────────────────
void MeanReversionStrategy::reset_derived() {
    window_.clear();
    sum_    = 0.0;
    sum2_   = 0.0;
    mean_   = 0.0;
    stddev_ = 0.0;
    z_      = 0.0;
}

// ── is_warmed_up ──────────────────────────────────────────────────────────────
bool MeanReversionStrategy::is_warmed_up() const noexcept {
    return window_.size() >= params_.lookback;
}

} // namespace strat
