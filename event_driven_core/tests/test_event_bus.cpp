#include <catch2/catch_test_macros.hpp>
#include "event_driven_core/event_bus.hpp"
#include <atomic>
#include <chrono>
#include <vector>

using namespace event_driven_core;

TEST_CASE("EventBus - Event Subscription", "[event_bus]") {
    EventBus bus;

    SECTION("Subscribe and receive MarketEvent") {
        std::atomic<bool> received{false};

        bus.subscribe(static_cast<uint32_t>(EventType::MARKET_EVENT),
                     [&received](const EventPtr& event) {
                         auto market_event = std::dynamic_pointer_cast<MarketEvent>(event);
                         if (market_event) {
                             received.store(true, std::memory_order_release);
                         }
                     });

        auto event = std::make_shared<MarketEvent>();
        event->symbol_id = 100;
        event->bid = 99.5;
        event->ask = 100.5;

        bus.publish(event);
        bus.process_events();

        REQUIRE(received.load(std::memory_order_acquire));
    }
}

TEST_CASE("EventBus - Multiple Event Types", "[event_bus]") {
    EventBus bus;
    std::atomic<int> market_count{0};
    std::atomic<int> signal_count{0};

    bus.subscribe(static_cast<uint32_t>(EventType::MARKET_EVENT),
                 [&market_count](const EventPtr&) {
                     market_count.fetch_add(1, std::memory_order_release);
                 });

    bus.subscribe(static_cast<uint32_t>(EventType::SIGNAL_EVENT),
                 [&signal_count](const EventPtr&) {
                     signal_count.fetch_add(1, std::memory_order_release);
                 });

    for (int i = 0; i < 5; ++i) {
        bus.publish(std::make_shared<MarketEvent>());
        bus.publish(std::make_shared<SignalEvent>());
    }

    bus.process_events();

    REQUIRE(market_count.load(std::memory_order_acquire) == 5);
    REQUIRE(signal_count.load(std::memory_order_acquire) == 5);
}

TEST_CASE("EventBus - Multiple Subscribers", "[event_bus]") {
    EventBus bus;
    std::atomic<int> handler1_count{0};
    std::atomic<int> handler2_count{0};

    bus.subscribe(static_cast<uint32_t>(EventType::ORDER_EVENT),
                 [&handler1_count](const EventPtr&) {
                     handler1_count.fetch_add(1, std::memory_order_release);
                 });

    bus.subscribe(static_cast<uint32_t>(EventType::ORDER_EVENT),
                 [&handler2_count](const EventPtr&) {
                     handler2_count.fetch_add(1, std::memory_order_release);
                 });

    bus.publish(std::make_shared<OrderEvent>());
    bus.process_events();

    REQUIRE(handler1_count.load(std::memory_order_acquire) == 1);
    REQUIRE(handler2_count.load(std::memory_order_acquire) == 1);
}

TEST_CASE("EventBus - Event Processing Order", "[event_bus]") {
    EventBus bus;
    std::vector<int> order;
    std::mutex order_mutex;

    bus.subscribe(static_cast<uint32_t>(EventType::MARKET_EVENT),
                 [&order, &order_mutex](const EventPtr& event) {
                     auto market_event = std::dynamic_pointer_cast<MarketEvent>(event);
                     if (market_event) {
                         std::lock_guard<std::mutex> lock(order_mutex);
                         order.push_back(market_event->symbol_id);
                     }
                 });

    for (int i = 0; i < 10; ++i) {
        auto event = std::make_shared<MarketEvent>();
        event->symbol_id = i;
        bus.publish(event);
    }

    bus.process_events();

    REQUIRE(order.size() == 10);
    for (size_t i = 0; i < order.size(); ++i) {
        REQUIRE(order[i] == static_cast<int>(i));
    }
}

TEST_CASE("EventBus - All Event Types", "[event_bus]") {
    EventBus bus;
    std::atomic<int> total_received{0};

    auto counter = [&total_received](const EventPtr&) {
        total_received.fetch_add(1, std::memory_order_release);
    };

    bus.subscribe(static_cast<uint32_t>(EventType::MARKET_EVENT), counter);
    bus.subscribe(static_cast<uint32_t>(EventType::SIGNAL_EVENT), counter);
    bus.subscribe(static_cast<uint32_t>(EventType::ORDER_EVENT), counter);
    bus.subscribe(static_cast<uint32_t>(EventType::FILL_EVENT), counter);
    bus.subscribe(static_cast<uint32_t>(EventType::RISK_EVENT), counter);

    bus.publish(std::make_shared<MarketEvent>());
    bus.publish(std::make_shared<SignalEvent>());
    bus.publish(std::make_shared<OrderEvent>());
    bus.publish(std::make_shared<FillEvent>());
    bus.publish(std::make_shared<RiskEvent>());

    bus.process_events();

    REQUIRE(total_received.load(std::memory_order_acquire) == 5);
}

TEST_CASE("EventBus - Clear Events", "[event_bus]") {
    EventBus bus;
    std::atomic<int> received{0};

    bus.subscribe(static_cast<uint32_t>(EventType::MARKET_EVENT),
                 [&received](const EventPtr&) {
                     received.fetch_add(1, std::memory_order_release);
                 });

    for (int i = 0; i < 10; ++i) {
        bus.publish(std::make_shared<MarketEvent>());
    }

    bus.clear();
    bus.process_events();

    REQUIRE(received.load(std::memory_order_acquire) == 0);
}
