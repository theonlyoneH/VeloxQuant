#pragma once

#include <atomic>
#include <memory>
#include <cstring>
#include <optional>
#include <stdexcept>

namespace event_driven_core {

// SPSC (Single Producer Single Consumer) lock-free queue
template <typename T>
class SPSCQueue {
private:
    static constexpr size_t CACHE_LINE_SIZE = 64;

    struct alignas(CACHE_LINE_SIZE) Slot {
        T value;
        std::atomic<bool> ready{false};
    };

    std::unique_ptr<Slot[]> buffer_;
    size_t capacity_;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> write_pos_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> read_pos_{0};

public:
    explicit SPSCQueue(size_t capacity) : capacity_(capacity) {
        if (capacity == 0 || (capacity & (capacity - 1)) != 0) {
            throw std::invalid_argument("Capacity must be a power of 2");
        }
        buffer_ = std::make_unique<Slot[]>(capacity);
    }

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    bool enqueue(const T& value) {
        size_t write_pos = write_pos_.load(std::memory_order_relaxed);
        size_t next_pos = (write_pos + 1) & (capacity_ - 1);
        size_t read_pos = read_pos_.load(std::memory_order_acquire);

        if (next_pos == read_pos) {
            return false;  // Queue full
        }

        buffer_[write_pos].value = value;
        buffer_[write_pos].ready.store(true, std::memory_order_release);
        write_pos_.store(next_pos, std::memory_order_release);
        return true;
    }

    std::optional<T> dequeue() {
        size_t read_pos = read_pos_.load(std::memory_order_relaxed);
        size_t write_pos = write_pos_.load(std::memory_order_acquire);

        if (read_pos == write_pos) {
            return std::nullopt;  // Queue empty
        }

        if (!buffer_[read_pos].ready.load(std::memory_order_acquire)) {
            return std::nullopt;
        }

        T value = buffer_[read_pos].value;
        buffer_[read_pos].ready.store(false, std::memory_order_relaxed);
        read_pos_.store((read_pos + 1) & (capacity_ - 1), std::memory_order_release);
        return value;
    }

    size_t size() const {
        size_t write = write_pos_.load(std::memory_order_acquire);
        size_t read = read_pos_.load(std::memory_order_acquire);
        return (write - read) & (capacity_ - 1);
    }

    bool empty() const {
        return write_pos_.load(std::memory_order_acquire) == read_pos_.load(std::memory_order_acquire);
    }
};

// MPSC (Multi Producer Single Consumer) lock-free queue
template <typename T>
class MPSCQueue {
private:
    static constexpr size_t CACHE_LINE_SIZE = 64;

    struct alignas(CACHE_LINE_SIZE) Node {
        T value;
        std::atomic<Node*> next{nullptr};
    };

    alignas(CACHE_LINE_SIZE) std::atomic<Node*> head_;
    alignas(CACHE_LINE_SIZE) Node* tail_;

public:
    MPSCQueue() {
        tail_ = new Node();
        head_.store(tail_, std::memory_order_relaxed);
    }

    ~MPSCQueue() {
        Node* node = tail_;
        while (node != nullptr) {
            Node* tmp = node;
            node = node->next.load(std::memory_order_relaxed);
            delete tmp;
        }
    }

    MPSCQueue(const MPSCQueue&) = delete;
    MPSCQueue& operator=(const MPSCQueue&) = delete;

    void enqueue(const T& value) {
        Node* new_node = new Node();
        new_node->value = value;

        Node* prev_head = head_.exchange(new_node, std::memory_order_acq_rel);
        prev_head->next.store(new_node, std::memory_order_release);
    }

    std::optional<T> dequeue() {
        Node* tail = tail_;
        Node* next = tail->next.load(std::memory_order_acquire);

        if (next == nullptr) {
            return std::nullopt;  // Queue empty
        }

        T value = next->value;
        tail_ = next;
        delete tail;
        return value;
    }

    bool empty() const {
        Node* tail = tail_;
        Node* next = tail->next.load(std::memory_order_acquire);
        return next == nullptr;
    }
};

}  // namespace event_driven_core
