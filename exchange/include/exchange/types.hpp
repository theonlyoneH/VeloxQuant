#pragma once
// exchange/types.hpp  ── Core domain types for the Exchange Simulator
//
// Design notes:
//  • Prices and quantities re-use md::Price / md::Quantity (fixed-point int64).
//  • OrderId is a monotonically increasing uint64 assigned by OrderRouter.
//  • All structs are value-semantic; the hot path owns Orders by value in
//    unordered_maps keyed by OrderId.
//  • FillId is similarly monotone – assigned by MatchingEngine.
//  • No virtual dispatch; polymorphism via variant + std::visit.

#include "market_data/types.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace exch {

using md::Price;
using md::Quantity;
using md::Timestamp;
using md::SymbolId;
using md::Side;
using md::to_price;
using md::from_price;

// ── Identifiers ───────────────────────────────────────────────────────────────
using OrderId = uint64_t;
using FillId  = uint64_t;

inline constexpr OrderId kInvalidOrderId = 0;
inline constexpr FillId  kInvalidFillId  = 0;

// ── Order classification enums ────────────────────────────────────────────────

/// Instruction type of an order
enum class OrderType : uint8_t {
    Market = 0,  ///< Fill at best available price
    Limit  = 1,  ///< Fill only at limit_price or better; rest if not filled
    Stop   = 2,  ///< Becomes Market when trigger_price is touched
    IOC    = 3,  ///< Immediate-Or-Cancel: fill what you can, cancel rest
    FOK    = 4,  ///< Fill-Or-Kill: must fill entirely or cancel entirely
};

/// Time-in-force qualifier (for Limit orders)
enum class TimeInForce : uint8_t {
    GTC = 0,  ///< Good-Till-Cancelled
    DAY = 1,  ///< Cancelled at end-of-session (caller's responsibility)
    IOC = 2,  ///< Immediate-Or-Cancel (mirrors OrderType::IOC)
    FOK = 3,  ///< Fill-Or-Kill        (mirrors OrderType::FOK)
};

/// Lifecycle status of an order
enum class OrderStatus : uint8_t {
    New             = 0,
    PartiallyFilled = 1,
    Filled          = 2,
    Cancelled       = 3,
    Rejected        = 4,
    PendingCancel   = 5,
    Triggered       = 6,  ///< Stop order has been triggered → converts to Market
};

/// Rejection reason
enum class RejectReason : uint8_t {
    None            = 0,
    InsufficientLiquidity = 1,  ///< FOK could not fill in full
    InvalidPrice    = 2,
    InvalidQuantity = 3,
    UnknownSymbol   = 4,
    DuplicateOrderId= 5,
    MarketClosed    = 6,
};

// ── Order ─────────────────────────────────────────────────────────────────────
/// Represents one order through its full lifecycle.
/// Mutable fields (filled_qty, status) updated in-place by the engine.
struct Order {
    OrderId     id;
    SymbolId    symbol_id;
    Side        side;
    OrderType   type;
    TimeInForce tif        {TimeInForce::GTC};
    Price       limit_price{0};    ///< For Limit / IOC / FOK
    Price       stop_price {0};    ///< Trigger price for Stop orders
    Quantity    qty        {0};    ///< Original order quantity
    Quantity    filled_qty {0};    ///< Cumulative quantity filled so far
    OrderStatus status     {OrderStatus::New};
    RejectReason reject_reason{RejectReason::None};
    Timestamp   submit_ts  {0};    ///< Nanoseconds when submitted
    Timestamp   last_fill_ts{0};   ///< Nanoseconds of most recent fill
    uint64_t    queue_pos  {0};    ///< Estimated shares ahead in queue (Limit only)

    // ── Derived helpers ───────────────────────────────────────────────────────
    [[nodiscard]] Quantity leaves_qty() const noexcept {
        return qty - filled_qty;
    }
    [[nodiscard]] bool is_done() const noexcept {
        return status == OrderStatus::Filled
            || status == OrderStatus::Cancelled
            || status == OrderStatus::Rejected;
    }
    [[nodiscard]] bool is_buy()  const noexcept { return side == Side::Buy;  }
    [[nodiscard]] bool is_sell() const noexcept { return side == Side::Sell; }
};

// ── Fill ──────────────────────────────────────────────────────────────────────
/// One execution report for a partial or full fill.
struct Fill {
    FillId      id;
    OrderId     order_id;
    SymbolId    symbol_id;
    Side        side;
    Price       fill_price;    ///< Actual execution price (fixed-point)
    Quantity    fill_qty;      ///< Quantity executed in this fill
    Price       commission;    ///< Commission charged (fixed-point)
    Price       slippage;      ///< Signed slippage vs. arrival price (fixed-point)
    Timestamp   timestamp;     ///< Execution timestamp (ns)
    bool        is_aggressive; ///< True if this order was the aggressor (taker)
};

// ── ExecutionReport ───────────────────────────────────────────────────────────
/// Snapshot of order state after an event (fill, cancel, reject).
struct ExecutionReport {
    Order  order;          ///< Full order state at event time
    Fill   last_fill;      ///< Most recent fill (qty==0 if no fill this event)
    bool   has_fill{false};
};

// ── C++20 Concepts ────────────────────────────────────────────────────────────

/// Callable receiving a Fill
template<typename F>
concept FillSink = std::invocable<F, const Fill&>;

/// Callable receiving an ExecutionReport
template<typename F>
concept ReportSink = std::invocable<F, const ExecutionReport&>;

// ── PriceLevel ────────────────────────────────────────────────────────────────
/// Aggregate view of one price level (for depth / slippage calculation)
struct PriceLevel {
    Price    price;
    Quantity total_qty;   ///< Sum of all resting order quantities at this level
    uint32_t order_count;
};

} // namespace exch
