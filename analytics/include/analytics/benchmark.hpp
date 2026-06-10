#pragma once
// analytics/benchmark.hpp  ── Micro-benchmark harness
//
// Self-contained benchmark framework using CLOCK_MONOTONIC.
// No external dependencies (does not require Google Benchmark).
//
// Usage:
//   auto result = bench::run("MyBench", 10'000, []{ do_work(); });
//   bench::print(result);

#include "analytics/stats.hpp"
#include "analytics/timer.hpp"

#include <functional>
#include <string>
#include <vector>

namespace anlt {

// ── BenchmarkResult ───────────────────────────────────────────────────────────
struct BenchmarkResult {
    std::string name;
    std::size_t iterations  {0};
    DescriptiveStats latency_ns;  ///< Per-iteration latency (nanoseconds)
    double throughput_ops_per_sec {0.0};
    double total_elapsed_ns       {0.0};
};

// ── Run a benchmark ───────────────────────────────────────────────────────────

/// Run `fn` for `warmup` iterations (discarded), then `iterations` measured.
/// Returns latency statistics in nanoseconds per iteration.
BenchmarkResult run(std::string_view name,
                    std::size_t iterations,
                    std::function<void()> fn,
                    std::size_t warmup = 100);

/// Print result to stdout in a readable table row.
void print(const BenchmarkResult& r);

/// Print a collection of results as a formatted table.
void print_table(const std::vector<BenchmarkResult>& results);

// ── BenchmarkSuite ────────────────────────────────────────────────────────────
/// Collects multiple benchmark results and can dump a summary table.
class BenchmarkSuite {
public:
    explicit BenchmarkSuite(std::string name) : name_(std::move(name)) {}

    void add(std::string_view bench_name,
             std::size_t iterations,
             std::function<void()> fn,
             std::size_t warmup = 100);

    void print_summary() const;

    [[nodiscard]] const std::vector<BenchmarkResult>& results() const noexcept {
        return results_;
    }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }

private:
    std::string name_;
    std::vector<BenchmarkResult> results_;
};

} // namespace anlt
