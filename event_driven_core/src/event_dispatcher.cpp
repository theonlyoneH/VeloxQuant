#include "event_driven_core/event_dispatcher.hpp"
#include <chrono>

namespace event_driven_core {

EventDispatcher::EventDispatcher(size_t num_worker_threads)
    : event_bus_(std::make_unique<EventBus>()),
      thread_pool_(std::make_unique<ThreadPool>(num_worker_threads)) {}

EventDispatcher::~EventDispatcher() {
    stop_async_processing();
}

void EventDispatcher::subscribe(std::uint32_t event_type, const EventBus::EventHandler& handler) {
    event_bus_->subscribe(event_type, handler);
}

void EventDispatcher::publish(const EventPtr& event) {
    event_bus_->publish(event);
}

void EventDispatcher::process() {
    event_bus_->process_events();
}

void EventDispatcher::start_async_processing() {
    if (async_processing_.load(std::memory_order_acquire)) {
        return;  // Already running
    }

    async_processing_.store(true, std::memory_order_release);
    processor_thread_ = std::make_unique<std::thread>([this]() { async_processor_thread(); });
}

void EventDispatcher::stop_async_processing() {
    async_processing_.store(false, std::memory_order_release);

    if (processor_thread_ && processor_thread_->joinable()) {
        processor_thread_->join();
        processor_thread_.reset();
    }
}

bool EventDispatcher::is_processing() const {
    return async_processing_.load(std::memory_order_acquire);
}

EventBus& EventDispatcher::bus() {
    return *event_bus_;
}

void EventDispatcher::async_processor_thread() {
    while (async_processing_.load(std::memory_order_acquire)) {
        event_bus_->process_events();
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    // Final drain
    event_bus_->process_events();
}

}  // namespace event_driven_core
