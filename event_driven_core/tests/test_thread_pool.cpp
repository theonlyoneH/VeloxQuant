#include <catch2/catch_test_macros.hpp>
#include "event_driven_core/thread_pool.hpp"
#include <atomic>
#include <chrono>
#include <vector>

using namespace event_driven_core;

TEST_CASE("ThreadPool - Basic Operations", "[thread_pool]") {
    SECTION("Create and shutdown") {
        ThreadPool pool(4);
        REQUIRE(pool.is_running());
        pool.shutdown();
        REQUIRE(!pool.is_running());
    }

    SECTION("Submit single task") {
        ThreadPool pool(2);
        std::atomic<int> counter{0};

        pool.submit([&counter]() {
            counter.store(42, std::memory_order_release);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        REQUIRE(counter.load(std::memory_order_acquire) == 42);
    }
}

TEST_CASE("ThreadPool - Task Execution", "[thread_pool]") {
    ThreadPool pool(4);

    SECTION("Multiple tasks") {
        std::atomic<int> counter{0};

        for (int i = 0; i < 100; ++i) {
            pool.submit([&counter]() {
                counter.fetch_add(1, std::memory_order_release);
            });
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        REQUIRE(counter.load(std::memory_order_acquire) == 100);
    }

    SECTION("Tasks with parameters") {
        std::vector<int> results;
        std::mutex results_mutex;

        for (int i = 0; i < 10; ++i) {
            pool.submit([i, &results, &results_mutex]() {
                std::lock_guard<std::mutex> lock(results_mutex);
                results.push_back(i * 2);
            });
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        REQUIRE(results.size() == 10);
    }
}

TEST_CASE("ThreadPool - Concurrent Execution", "[thread_pool]") {
    ThreadPool pool(8);

    SECTION("High concurrency") {
        std::atomic<int> completed{0};

        for (int i = 0; i < 1000; ++i) {
            pool.submit([&completed]() {
                std::this_thread::sleep_for(std::chrono::microseconds(10));
                completed.fetch_add(1, std::memory_order_release);
            });
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        REQUIRE(completed.load(std::memory_order_acquire) == 1000);
    }
}

TEST_CASE("ThreadPool - Error Handling", "[thread_pool]") {
    ThreadPool pool(2);

    SECTION("Submit after shutdown") {
        pool.shutdown();
        REQUIRE_THROWS(pool.submit([]() {}));
    }
}

TEST_CASE("ThreadPool - Worker Count", "[thread_pool]") {
    SECTION("Single worker") {
        ThreadPool pool(1);
        REQUIRE(pool.is_running());
    }

    SECTION("Multiple workers") {
        ThreadPool pool(16);
        REQUIRE(pool.is_running());
    }
}
