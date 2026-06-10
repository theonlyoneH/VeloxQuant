#pragma once
// analytics/timer.hpp  ── High-resolution wall-clock timer (CLOCK_MONOTONIC)
//
// Uses clock_gettime(CLOCK_MONOTONIC) – available on all Linux systems.
// Provides nanosecond-resolution elapsed time measurement.

#include <cstdint>
#include <ctime>

namespace anlt {

struct Timer {
    void start() noexcept {
        clock_gettime(CLOCK_MONOTONIC, &t0_);
    }
    void stop() noexcept {
        clock_gettime(CLOCK_MONOTONIC, &t1_);
    }
    /// Elapsed nanoseconds between start() and stop()
    [[nodiscard]] int64_t elapsed_ns() const noexcept {
        return (t1_.tv_sec - t0_.tv_sec) * 1'000'000'000LL
             + (t1_.tv_nsec - t0_.tv_nsec);
    }
    [[nodiscard]] double elapsed_us() const noexcept {
        return static_cast<double>(elapsed_ns()) * 1e-3;
    }
    [[nodiscard]] double elapsed_ms() const noexcept {
        return static_cast<double>(elapsed_ns()) * 1e-6;
    }

private:
    timespec t0_{}, t1_{};
};

/// RAII scoped timer: measures lifetime of the scope
struct ScopedTimer {
    explicit ScopedTimer(int64_t& out_ns) noexcept : out_(out_ns) {
        timer_.start();
    }
    ~ScopedTimer() noexcept {
        timer_.stop();
        out_ = timer_.elapsed_ns();
    }
private:
    Timer   timer_;
    int64_t& out_;
};

} // namespace anlt
