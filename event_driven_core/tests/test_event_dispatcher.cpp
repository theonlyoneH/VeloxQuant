#include <catch2/catch_test_macros.hpp>
#include "event_driven_core/event_dispatcher.hpp"
#include <atomic>
#include <chrono>

using namespace event_driven_core;

TEST_CASE("EventDispatcher - Synchronous Processing", "[event_dispatcher]") {
    EventDispatcher dispatcher(4);
    std::atomic<int> received{0};

    dispatcher.subscribe(static_cast<uint32_t>(EventType::MARKET_EVENT),
                        [&received](const EventPtr&) {
                            received.fetch_add(1, std::memory_order_release);
                        });

    auto event = std::make_shared<MarketEvent>();
    event->symbol_id = 100;

    dispatcher.publish(event);
    dispatcher.process();

    REQUIRE(received.load(std::memory_order_acquire) == 1);
}

TEST_CASE("EventDispatcher - Asynchronous Processing", "[event_dispatcher]") {
    EventDispatcher dispatcher(4);
    std::atomic<int> received{0};

    dispatcher.subscribe(static_cast<uint32_t>(EventType::SIGNAL_EVENT),
                        [&received](const EventPtr&) {
                            received.fetch_add(1, std::memory_order_release);
                        });

    dispatcher.start_async_processing();
    REQUIRE(dispatcher.is_processing());

    for (int i = 0; i < 10; ++i) {
        dispatcher.publish(std::make_shared<SignalEvent>());
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    dispatcher.stop_async_processing();

    REQUIRE(!dispatcher.is_processing());
    REQUIRE(received.load(std::memory_order_acquire) == 10);
}

TEST_CASE("EventDispatcher - Event Routing", "[event_dispatcher]") {
    EventDispatcher dispatcher(2);
    std::atomic<int> market_count{0};
    std::atomic<int> order_count{0};

    dispatcher.subscribe(static_cast<uint32_t>(EventType::MARKET_EVENT),
                        [&market_count](const EventPtr&) {
                            market_count.fetch_add(1, std::memory_order_release);
                        });

    dispatcher.subscribe(static_cast<uint32_t>(EventType::ORDER_EVENT),
                        [&order_count](const EventPtr&) {
                            order_count.fetch_add(1, std::memory_order_release);
                        });

    dispatcher.publish(std::make_shared<MarketEvent>());
    dispatcher.publish(std::make_shared<OrderEvent>());
    dispatcher.publish(std::make_shared<MarketEvent>());

    dispatcher.process();

    REQUIRE(market_count.load(std::memory_order_acquire) == 2);
    REQUIRE(order_count.load(std::memory_order_acquire) == 1);
}

TEST_CASE("EventDispatcher - Multiple Event Types", "[event_dispatcher]") {
    EventDispatcher dispatcher(4);
    std::atomic<int> total{0};

    auto counter = [&total](const EventPtr&) {
        total.fetch_add(1, std::memory_order_release);
    };

    dispatcher.subscribe(static_cast<uint32_t>(EventType::MARKET_EVENT), counter);
    dispatcher.subscribe(static_cast<uint32_t>(EventType::SIGNAL_EVENT), counter);
    dispatcher.subscribe(static_cast<uint32_t>(EventType::ORDER_EVENT), counter);
    dispatcher.subscribe(static_cast<uint32_t>(EventType::FILL_EVENT), counter);
    dispatcher.subscribe(static_cast<uint32_t>(EventType::RISK_EVENT), counter);

    dispatcher.publish(std::make_shared<MarketEvent>());
    dispatcher.publish(std::make_shared<SignalEvent>());
    dispatcher.publish(std::make_shared<OrderEvent>());
    dispatcher.publish(std::make_shared<FillEvent>());
    dispatcher.publish(std::make_shared<RiskEvent>());

    dispatcher.process();

    REQUIRE(total.load(std::memory_order_acquire) == 5);
}

TEST_CASE("EventDispatcher - Bus Access", "[event_dispatcher]") {
    EventDispatcher dispatcher(2);
    EventBus& bus = dispatcher.bus();

    std::atomic<bool> received{false};
    bus.subscribe(static_cast<uint32_t>(EventType::MARKET_EVENT),
                 [&received](const EventPtr&) {
                     received.store(true, std::memory_order_release);
                 });

    dispatcher.publish(std::make_shared<MarketEvent>());
    dispatcher.process();

    REQUIRE(received.load(std::memory_order_acquire));
}

TEST_CASE("EventDispatcher - High Volume", "[event_dispatcher]") {
    EventDispatcher dispatcher(8);
    std::atomic<int> processed{0};

    dispatcher.subscribe(static_cast<uint32_t>(EventType::MARKET_EVENT),
                        [&processed](const EventPtr&) {
                            processed.fetch_add(1, std::memory_order_release);
                        });

    const int NUM_EVENTS = 10000;
    for (int i = 0; i < NUM_EVENTS; ++i) {
        dispatcher.publish(std::make_shared<MarketEvent>());
    }

    dispatcher.process();

    REQUIRE(processed.load(std::memory_order_acquire) == NUM_EVENTS);
}

TEST_CASE("EventDispatcher - Event Data Preservation", "[event_dispatcher]") {
    EventDispatcher dispatcher(2);
    std::atomic<double> received_price{0.0};

    dispatcher.subscribe(static_cast<uint32_t>(EventType::MARKET_EVENT),
                        [&received_price](const EventPtr& event) {
                            auto market_event = std::dynamic_pointer_cast<MarketEvent>(event);
                            if (market_event) {
                                received_price.store(market_event->bid, std::memory_order_release);
                            }
                        });

    auto event = std::make_shared<MarketEvent>();
    event->bid = 99.75;
    dispatcher.publish(event);
    dispatcher.process();

    REQUIRE(received_price.load(std::memory_order_acquire) == 99.75);
}
