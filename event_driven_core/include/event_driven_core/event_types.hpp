#pragma once

#include <cstdint>
#include <chrono>
#include <variant>
#include <memory>

namespace event_driven_core {

// Base event interface
struct Event {
    virtual ~Event() = default;
    virtual std::uint32_t type() const = 0;
};

// Event type identifiers
enum class EventType : std::uint32_t {
    MARKET_EVENT = 1,
    SIGNAL_EVENT = 2,
    ORDER_EVENT = 3,
    FILL_EVENT = 4,
    RISK_EVENT = 5
};

// MarketEvent: Represents market data updates
struct MarketEvent : Event {
    std::uint64_t symbol_id;
    double bid;
    double ask;
    std::uint64_t bid_size;
    std::uint64_t ask_size;
    std::int64_t timestamp_ns;  // nanoseconds since epoch

    MarketEvent() : symbol_id(0), bid(0), ask(0), bid_size(0), ask_size(0), timestamp_ns(0) {}

    std::uint32_t type() const override {
        return static_cast<std::uint32_t>(EventType::MARKET_EVENT);
    }
};

// SignalEvent: Represents trading signals from strategy
struct SignalEvent : Event {
    std::uint64_t symbol_id;
    std::int32_t signal;  // -1: sell, 0: hold, 1: buy
    double strength;      // 0.0-1.0
    std::int64_t timestamp_ns;

    SignalEvent() : symbol_id(0), signal(0), strength(0), timestamp_ns(0) {}

    std::uint32_t type() const override {
        return static_cast<std::uint32_t>(EventType::SIGNAL_EVENT);
    }
};

// OrderEvent: Represents order placement
struct OrderEvent : Event {
    std::uint64_t order_id;
    std::uint64_t symbol_id;
    std::int32_t quantity;
    double price;
    enum class Side { BUY = 1, SELL = 2 } side;
    std::int64_t timestamp_ns;

    OrderEvent() : order_id(0), symbol_id(0), quantity(0), price(0), side(Side::BUY), timestamp_ns(0) {}

    std::uint32_t type() const override {
        return static_cast<std::uint32_t>(EventType::ORDER_EVENT);
    }
};

// FillEvent: Represents order fills
struct FillEvent : Event {
    std::uint64_t order_id;
    std::uint64_t symbol_id;
    std::int32_t filled_quantity;
    double filled_price;
    enum class Side { BUY = 1, SELL = 2 } side;
    std::int64_t timestamp_ns;

    FillEvent() : order_id(0), symbol_id(0), filled_quantity(0), filled_price(0), side(Side::BUY), timestamp_ns(0) {}

    std::uint32_t type() const override {
        return static_cast<std::uint32_t>(EventType::FILL_EVENT);
    }
};

// RiskEvent: Represents risk limit violations
struct RiskEvent : Event {
    enum class RiskType { LOSS_LIMIT = 1, LEVERAGE_LIMIT = 2, DRAWDOWN_LIMIT = 3 } risk_type;
    double current_value;
    double threshold_value;
    std::string description;
    std::int64_t timestamp_ns;

    RiskEvent() : risk_type(RiskType::LOSS_LIMIT), current_value(0), threshold_value(0), timestamp_ns(0) {}

    std::uint32_t type() const override {
        return static_cast<std::uint32_t>(EventType::RISK_EVENT);
    }
};

using EventPtr = std::shared_ptr<Event>;

}  // namespace event_driven_core
