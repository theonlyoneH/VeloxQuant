#pragma once

#include "lock_free_queue.hpp"
#include <thread>
#include <vector>
#include <functional>
#include <atomic>
#include <memory>

namespace event_driven_core {

class ThreadPool {
public:
    using Task = std::function<void()>;

    explicit ThreadPool(size_t num_threads);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void submit(const Task& task);
    void shutdown();
    bool is_running() const;

private:
    void worker_thread();

    std::vector<std::thread> threads_;
    std::unique_ptr<MPSCQueue<Task>> task_queue_;
    std::atomic<bool> running_{false};
    std::atomic<bool> shutdown_requested_{false};
};

}  // namespace event_driven_core
