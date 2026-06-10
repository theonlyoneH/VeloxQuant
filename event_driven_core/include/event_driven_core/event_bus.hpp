#pragma once

#include "event_types.hpp"
#include "lock_free_queue.hpp"
#include <functional>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>

namespace event_driven_core {

class EventBus {
public:
    using EventHandler = std::function<void(const EventPtr&)>;

    EventBus();
    ~EventBus() = default;

    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    // Subscribe to events of a specific type
    void subscribe(std::uint32_t event_type, const EventHandler& handler);

    // Publish an event
    void publish(const EventPtr& event);

    // Process all pending events
    void process_events();

    // Get number of pending events
    size_t pending_events() const;

    // Clear all events
    void clear();

private:
    std::unique_ptr<MPSCQueue<EventPtr>> event_queue_;
    std::unordered_map<std::uint32_t, std::vector<EventHandler>> subscribers_;
    mutable std::mutex subscribers_mutex_;
};

}  // namespace event_driven_core
