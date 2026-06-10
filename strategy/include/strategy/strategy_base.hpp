#pragma once
// strategy/strategy_base.hpp  ── Common interface and mixin for all strategies
//
// Design:
//  • IStrategy is a pure abstract interface – every strategy implements it.
//  • StrategyBase<Derived> is a CRTP helper that:
//    – Owns a rolling price history (std::deque<double>) up to `max_history` bars.
//    – Provides push_price() which appends and trims the history.
//    – Stores the SignalCallback.
//    – Tracks bar count, current timestamp, and last signal emitted.
//  • Strategies never allocate on the hot path after initialisation.
//  • The StrategyLike concept constrains generic simulation harnesses.

#include "strategy/signal.hpp"
#include "market_data/types.hpp"

#include <concepts>
#include <deque>
#include <functional>
#include <string_view>

namespace strat {

using md::Bar;
using md::Tick;

// ── IStrategy ─────────────────────────────────────────────────────────────────
class IStrategy {
public:
    virtual ~IStrategy() = default;

    /// Called once per completed bar (primary update path).
    virtual void on_bar(const Bar& bar) = 0;

    /// Called on each raw tick (for tick-level strategies).
    virtual void on_tick(const Tick& tick) = 0;

    /// Reset all state (e.g. for walk-forward re-initialisation).
    virtual void reset() = 0;

    /// Human-readable strategy name (string literal).
    [[nodiscard]] virtual const char* name() const noexcept = 0;

    /// Register callback fired when a new signal is generated.
    virtual void on_signal(SignalCallback cb) = 0;

    /// Last signal emitted (Flat if none yet).
    [[nodiscard]] virtual Signal last_signal() const noexcept = 0;
};

// ── StrategyBase<Derived> ─────────────────────────────────────────────────────
template<typename Derived>
class StrategyBase : public IStrategy {
public:
    explicit StrategyBase(SymbolId symbol_id, std::size_t max_history) noexcept
        : symbol_id_(symbol_id), max_history_(max_history)
    {}

    // Default tick handler: extract close price and delegate to on_bar-like logic
    void on_tick(const Tick& tick) override {
        // Default: strategies operate on bars, not raw ticks.
        // Derived classes may override.
        (void)tick;
    }

    void on_signal(SignalCallback cb) override {
        signal_cb_ = std::move(cb);
    }

    [[nodiscard]] Signal last_signal() const noexcept override {
        return last_signal_;
    }

    void reset() override {
        history_.clear();
        bar_count_ = 0;
        last_signal_ = Signal{};
        static_cast<Derived*>(this)->reset_derived();
    }

protected:
    // ── Price history management ──────────────────────────────────────────────
    void push_price(double price) {
        history_.push_back(price);
        if (history_.size() > max_history_)
            history_.pop_front();
        ++bar_count_;
    }

    /// Emit a signal via callback and store as last_signal_
    void emit(SignalType type, double strength, Timestamp ts) {
        last_signal_ = Signal{
            .symbol_id = symbol_id_,
            .timestamp = ts,
            .type      = type,
            .strength  = strength,
            .source    = static_cast<Derived*>(this)->name(),
        };
        if (signal_cb_) signal_cb_(last_signal_);
    }

    // ── State ─────────────────────────────────────────────────────────────────
    SymbolId             symbol_id_;
    std::size_t          max_history_;
    std::deque<double>   history_;    ///< Rolling window of close prices
    std::size_t          bar_count_{0};
    Signal               last_signal_{};
    SignalCallback       signal_cb_;
};

// ── C++20 Concept ─────────────────────────────────────────────────────────────
template<typename T>
concept StrategyLike = requires(T t, const Bar& b, const Tick& tick) {
    { t.on_bar(b)   } -> std::same_as<void>;
    { t.on_tick(tick) } -> std::same_as<void>;
    { t.name()      } -> std::convertible_to<const char*>;
    { t.reset()     } -> std::same_as<void>;
};

} // namespace strat
