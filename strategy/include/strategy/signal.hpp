#pragma once
// strategy/signal.hpp  ── Signal type emitted by all strategy implementations
//
// Design:
//  • A Signal is a lightweight value type passed by callback to the caller.
//  • strength ∈ [-1.0, 1.0]: negative = bearish, positive = bullish.
//    Magnitude encodes conviction; |1.0| = maximum conviction.
//  • SignalType is the discrete direction; strength allows fractional sizing.
//  • source is a short name string for logging / attribution.

#include "market_data/types.hpp"
#include <string_view>
#include <functional>

namespace strat {

using md::SymbolId;
using md::Timestamp;

// ── SignalType ────────────────────────────────────────────────────────────────
enum class SignalType : int8_t {
    Short = -1,  ///< Go / stay short
    Flat  =  0,  ///< No position (exit or hold flat)
    Long  =  1,  ///< Go / stay long
};

[[nodiscard]] constexpr std::string_view to_string(SignalType s) noexcept {
    switch (s) {
    case SignalType::Short: return "SHORT";
    case SignalType::Flat:  return "FLAT";
    case SignalType::Long:  return "LONG";
    }
    return "UNKNOWN";
}

// ── Signal ───────────────────────────────────────────────────────────────────
struct Signal {
    SymbolId    symbol_id  {0};
    Timestamp   timestamp  {0};
    SignalType  type       {SignalType::Flat};
    double      strength   {0.0};  ///< [-1, 1]; magnitude = conviction
    const char* source     {""};   ///< Strategy name (string literal, not owned)

    [[nodiscard]] bool is_long()  const noexcept { return type == SignalType::Long;  }
    [[nodiscard]] bool is_short() const noexcept { return type == SignalType::Short; }
    [[nodiscard]] bool is_flat()  const noexcept { return type == SignalType::Flat;  }
};

// ── Callback type ─────────────────────────────────────────────────────────────
using SignalCallback = std::function<void(const Signal&)>;

// ── C++20 Concept ─────────────────────────────────────────────────────────────
template<typename F>
concept SignalSink = std::invocable<F, const Signal&>;

} // namespace strat
