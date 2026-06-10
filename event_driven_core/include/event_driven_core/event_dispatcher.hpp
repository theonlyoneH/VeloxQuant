#pragma once

#include "event_bus.hpp"
#include "thread_pool.hpp"
#include <atomic>
#include <memory>

namespace event_driven_core {

class EventDispatcher {
public:
    explicit EventDispatcher(size_t num_worker_threads);
    ~EventDispatcher();

    EventDispatcher(const EventDispatcher&) = delete;
    EventDispatcher& operator=(const EventDispatcher&) = delete;

    // Subscribe to events
    void subscribe(std::uint32_t event_type, const EventBus::EventHandler& handler);

    // Publish an event (async)
    void publish(const EventPtr& event);

    // Process pending events synchronously
    void process();

    // Start async processing loop
    void start_async_processing();

    // Stop async processing loop
    void stop_async_processing();

    // Check if async processing is running
    bool is_processing() const;

    // Get EventBus for advanced usage
    EventBus& bus();

private:
    void async_processor_thread();

    std::unique_ptr<EventBus> event_bus_;
    std::unique_ptr<ThreadPool> thread_pool_;
    std::atomic<bool> async_processing_{false};
    std::unique_ptr<std::thread> processor_thread_;
};

}  // namespace event_driven_core
