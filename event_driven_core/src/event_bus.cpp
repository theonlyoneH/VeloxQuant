#include "event_driven_core/event_bus.hpp"

namespace event_driven_core {

EventBus::EventBus() : event_queue_(std::make_unique<MPSCQueue<EventPtr>>()) {}

void EventBus::subscribe(std::uint32_t event_type, const EventHandler& handler) {
    std::lock_guard<std::mutex> lock(subscribers_mutex_);
    subscribers_[event_type].push_back(handler);
}

void EventBus::publish(const EventPtr& event) {
    if (event) {
        event_queue_->enqueue(event);
    }
}

void EventBus::process_events() {
    while (auto event = event_queue_->dequeue()) {
        std::lock_guard<std::mutex> lock(subscribers_mutex_);

        std::uint32_t event_type = event.value()->type();
        auto it = subscribers_.find(event_type);

        if (it != subscribers_.end()) {
            for (const auto& handler : it->second) {
                handler(event.value());
            }
        }
    }
}

size_t EventBus::pending_events() const {
    // Note: MPSC queue doesn't expose size directly
    // This is a best-effort check
    return 0;  // Could be enhanced with additional tracking
}

void EventBus::clear() {
    while (event_queue_->dequeue()) {
        // Drain queue
    }
}

}  // namespace event_driven_core
