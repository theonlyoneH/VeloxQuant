#include <catch2/catch_test_macros.hpp>
#include "event_driven_core/lock_free_queue.hpp"
#include <thread>
#include <vector>

using namespace event_driven_core;

TEST_CASE("SPSC Queue - Basic Operations", "[spsc_queue]") {
    SPSCQueue<int> queue(16);

    SECTION("Empty queue") {
        REQUIRE(queue.empty());
        REQUIRE(queue.dequeue() == std::nullopt);
    }

    SECTION("Enqueue and dequeue") {
        REQUIRE(queue.enqueue(42));
        REQUIRE(!queue.empty());
        auto value = queue.dequeue();
        REQUIRE(value.has_value());
        REQUIRE(value.value() == 42);
        REQUIRE(queue.empty());
    }

    SECTION("Multiple operations") {
        for (int i = 0; i < 10; ++i) {
            REQUIRE(queue.enqueue(i));
        }
        REQUIRE(queue.size() == 10);

        for (int i = 0; i < 10; ++i) {
            auto value = queue.dequeue();
            REQUIRE(value.has_value());
            REQUIRE(value.value() == i);
        }
        REQUIRE(queue.empty());
    }
}

TEST_CASE("SPSC Queue - Capacity", "[spsc_queue]") {
    SPSCQueue<int> queue(8);

    SECTION("Queue fills up") {
        for (int i = 0; i < 7; ++i) {
            REQUIRE(queue.enqueue(i));
        }
        REQUIRE(!queue.enqueue(99));  // Should fail when full
    }

    SECTION("Reuse after dequeue") {
        REQUIRE(queue.enqueue(1));
        REQUIRE(!queue.enqueue(2));  // Full
        auto val = queue.dequeue();
        REQUIRE(val.value() == 1);
        REQUIRE(queue.enqueue(2));  // Should succeed now
    }
}

TEST_CASE("SPSC Queue - Power of 2 validation", "[spsc_queue]") {
    SECTION("Valid capacity") {
        REQUIRE_NOTHROW(SPSCQueue<int>(16));
        REQUIRE_NOTHROW(SPSCQueue<int>(32));
        REQUIRE_NOTHROW(SPSCQueue<int>(1024));
    }

    SECTION("Invalid capacity") {
        REQUIRE_THROWS(SPSCQueue<int>(10));  // Not power of 2
        REQUIRE_THROWS(SPSCQueue<int>(0));   // Zero
    }
}

TEST_CASE("MPSC Queue - Basic Operations", "[mpsc_queue]") {
    MPSCQueue<int> queue;

    SECTION("Empty queue") {
        REQUIRE(queue.empty());
        REQUIRE(queue.dequeue() == std::nullopt);
    }

    SECTION("Single producer") {
        queue.enqueue(42);
        REQUIRE(!queue.empty());
        auto value = queue.dequeue();
        REQUIRE(value.has_value());
        REQUIRE(value.value() == 42);
        REQUIRE(queue.empty());
    }

    SECTION("Multiple enqueues") {
        for (int i = 0; i < 100; ++i) {
            queue.enqueue(i);
        }

        for (int i = 0; i < 100; ++i) {
            auto value = queue.dequeue();
            REQUIRE(value.has_value());
            REQUIRE(value.value() == i);
        }
        REQUIRE(queue.empty());
    }
}

TEST_CASE("MPSC Queue - Multiple Producers", "[mpsc_queue]") {
    MPSCQueue<int> queue;
    const int NUM_THREADS = 4;
    const int OPS_PER_THREAD = 100;

    SECTION("Concurrent producers") {
        std::vector<std::thread> threads;

        // Start producer threads
        for (int t = 0; t < NUM_THREADS; ++t) {
            threads.emplace_back([&queue, t]() {
                for (int i = 0; i < OPS_PER_THREAD; ++i) {
                    queue.enqueue(t * OPS_PER_THREAD + i);
                }
            });
        }

        // Wait for producers
        for (auto& t : threads) {
            t.join();
        }

        // Consume all items
        std::vector<int> consumed;
        while (auto val = queue.dequeue()) {
            consumed.push_back(val.value());
        }

        REQUIRE(consumed.size() == NUM_THREADS * OPS_PER_THREAD);
        REQUIRE(queue.empty());
    }
}

TEST_CASE("MPSC Queue - No capacity limits", "[mpsc_queue]") {
    MPSCQueue<int> queue;

    SECTION("Unlimited enqueue") {
        for (int i = 0; i < 10000; ++i) {
            queue.enqueue(i);
        }

        for (int i = 0; i < 10000; ++i) {
            auto value = queue.dequeue();
            REQUIRE(value.has_value());
            REQUIRE(value.value() == i);
        }
    }
}
