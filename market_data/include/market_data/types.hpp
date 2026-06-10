#pragma once
// market_data/types.hpp  ── Core domain types for market data
//
// Design notes:
//  • All timestamps are int64_t nanoseconds since Unix epoch (UTC).
//  • Prices use int64_t fixed-point (price * 1e8) to avoid float rounding.
//  • Sizes use uint64_t (number of shares / contracts in lot units).
//  • Types are trivially-copyable so they can be memcpy'd into mmap files.

#include <array>
#include <concepts>
#include <cstdint>
#include <string_view>

namespace md {

// ── Primitive aliases ──────────────────────────────────────────────────────────
using Timestamp  = int64_t;   ///< Nanoseconds since Unix epoch
using Price      = int64_t;   ///< Fixed-point: actual_price * 1e8
using Quantity   = uint64_t;  ///< Shares / contracts
using SymbolId   = uint32_t;  ///< Numeric symbol identifier (mapped externally)
using SequenceNo = uint64_t;  ///< Feed-level sequence counter

/// Convenience: encode a double price into fixed-point int64
[[nodiscard]] constexpr Price to_price(double d) noexcept {
    return static_cast<Price>(d * 1e8 + 0.5);
}

/// Decode fixed-point int64 back to double
[[nodiscard]] constexpr double from_price(Price p) noexcept {
    return static_cast<double>(p) * 1e-8;
}

// ── Tick ──────────────────────────────────────────────────────────────────────
/// A single level-1 market data update.
/// Size must be kept ≤ 64 bytes (fits in one cache line).
struct alignas(64) Tick {
    Timestamp  recv_ts;     ///< Feed-receive timestamp (ns)
    Timestamp  exch_ts;     ///< Exchange-reported timestamp (ns)
    SymbolId   symbol_id;   ///< Numeric symbol ID
    uint32_t   _pad0{};     ///< Reserved for alignment
    Price      bid_price;   ///< Best bid (fixed-point)
    Price      ask_price;   ///< Best ask (fixed-point)
    Price      last_price;  ///< Last trade price (fixed-point)
    Quantity   bid_size;    ///< Best bid quantity
    Quantity   ask_size;    ///< Best ask quantity
    Quantity   last_size;   ///< Last trade quantity
    SequenceNo seq_no;      ///< Monotonically increasing per-symbol sequence
};
static_assert(sizeof(Tick) <= 128, "Tick must fit in two cache lines");
static_assert(std::is_trivially_copyable_v<Tick>);

// ── OHLCV Bar ─────────────────────────────────────────────────────────────────
/// Aggregated bar (candle).  Fits in two cache lines.
struct alignas(64) Bar {
    Timestamp  recv_ts;     ///< Feed-receive timestamp (ns)
    Timestamp  open_ts;     ///< Bar open timestamp (ns)
    Timestamp  close_ts;    ///< Bar close timestamp (ns)
    SymbolId   symbol_id;
    uint32_t   _pad0{};
    Price      open;
    Price      high;
    Price      low;
    Price      close;
    Price      vwap;        ///< Volume-weighted average price
    Quantity   volume;
    uint64_t   trade_count;
};
static_assert(std::is_trivially_copyable_v<Bar>);

// ── BarType ───────────────────────────────────────────────────────────────────
enum class BarType : uint8_t {
    Second  = 0,
    Minute  = 1,
    Hour    = 2,
    Day     = 3,
    Tick    = 4,  ///< Trade-count-based bar
    Volume  = 5,  ///< Volume-based bar
};

// ── Side ─────────────────────────────────────────────────────────────────────
enum class Side : uint8_t { Buy = 0, Sell = 1, Unknown = 2 };

// ── C++20 Concepts ────────────────────────────────────────────────────────────

/// Any type that has a recv_ts member of type Timestamp
template<typename T>
concept TimestampedRecord = requires(T t) {
    { t.recv_ts } -> std::convertible_to<Timestamp>;
};

/// Any type with a symbol_id member
template<typename T>
concept SymbolTagged = requires(T t) {
    { t.symbol_id } -> std::convertible_to<SymbolId>;
};

/// Full market data record: timestamped + symbol-tagged
template<typename T>
concept MarketRecord = TimestampedRecord<T> && SymbolTagged<T>
    && std::is_trivially_copyable_v<T>;

/// Callable that can receive a Tick
template<typename F>
concept TickSink = std::invocable<F, const Tick&>;

/// Callable that can receive a Bar
template<typename F>
concept BarSink  = std::invocable<F, const Bar&>;

// ── Symbol map helpers ────────────────────────────────────────────────────────
/// Maximum length of a symbol ticker string (including null terminator)
inline constexpr std::size_t kMaxTickerLen = 16;

struct SymbolInfo {
    SymbolId                         id;
    std::array<char, kMaxTickerLen>  ticker;  ///< e.g. "AAPL\0..."
};

} // namespace md
