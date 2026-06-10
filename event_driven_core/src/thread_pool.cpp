#include "event_driven_core/thread_pool.hpp"
#include <chrono>

namespace event_driven_core {

ThreadPool::ThreadPool(size_t num_threads) : task_queue_(std::make_unique<MPSCQueue<Task>>()) {
    running_.store(true, std::memory_order_release);

    for (size_t i = 0; i < num_threads; ++i) {
        threads_.emplace_back([this]() { worker_thread(); });
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::submit(const Task& task) {
    if (!running_.load(std::memory_order_acquire)) {
        throw std::runtime_error("ThreadPool is not running");
    }
    task_queue_->enqueue(task);
}

void ThreadPool::shutdown() {
    shutdown_requested_.store(true, std::memory_order_release);
    running_.store(false, std::memory_order_release);

    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    threads_.clear();
}

bool ThreadPool::is_running() const {
    return running_.load(std::memory_order_acquire);
}

void ThreadPool::worker_thread() {
    while (running_.load(std::memory_order_acquire)) {
        auto task = task_queue_->dequeue();
        if (task) {
            task.value()();
        } else {
            // Brief sleep to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    }
}

}  // namespace event_driven_core
