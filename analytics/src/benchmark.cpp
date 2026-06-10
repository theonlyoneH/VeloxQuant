// analytics/benchmark.cpp
#include "analytics/benchmark.hpp"
#include <format>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace anlt {

// ── run ───────────────────────────────────────────────────────────────────────
BenchmarkResult run(std::string_view name,
                    std::size_t iterations,
                    std::function<void()> fn,
                    std::size_t warmup) {
    // Warmup (not measured)
    for (std::size_t i = 0; i < warmup; ++i) fn();

    // Measure individual iterations
    std::vector<double> samples;
    samples.reserve(iterations);

    Timer t;
    for (std::size_t i = 0; i < iterations; ++i) {
        t.start();
        fn();
        t.stop();
        samples.push_back(static_cast<double>(t.elapsed_ns()));
    }

    BenchmarkResult r;
    r.name       = name;
    r.iterations = iterations;
    r.latency_ns = describe(samples);

    // Total elapsed = sum of all measurements
    r.total_elapsed_ns = r.latency_ns.mean * static_cast<double>(iterations);
    r.throughput_ops_per_sec = (r.latency_ns.mean > 0.0)
        ? 1e9 / r.latency_ns.mean
        : 0.0;

    return r;
}

// ── print ─────────────────────────────────────────────────────────────────────
void print(const BenchmarkResult& r) {
    std::cout
        << std::left << std::setw(40) << r.name
        << " | n=" << std::setw(8) << r.iterations
        << " | mean=" << std::fixed << std::setprecision(1)
        << std::setw(10) << r.latency_ns.mean << "ns"
        << " | p50=" << std::setw(10) << r.latency_ns.p50 << "ns"
        << " | p99=" << std::setw(10) << r.latency_ns.p99 << "ns"
        << " | tput=" << std::setprecision(0)
        << r.throughput_ops_per_sec << " ops/s"
        << '\n';
}

// ── print_table ───────────────────────────────────────────────────────────────
void print_table(const std::vector<BenchmarkResult>& results) {
    std::cout << '\n'
              << std::string(100, '-') << '\n'
              << std::left
              << std::setw(40) << "Benchmark"
              << " | " << std::setw(8)  << "n"
              << " | " << std::setw(12) << "mean (ns)"
              << " | " << std::setw(12) << "p50 (ns)"
              << " | " << std::setw(12) << "p99 (ns)"
              << " | throughput\n"
              << std::string(100, '-') << '\n';
    for (const auto& r : results) print(r);
    std::cout << std::string(100, '-') << '\n';
}

// ── BenchmarkSuite ────────────────────────────────────────────────────────────
void BenchmarkSuite::add(std::string_view bench_name,
                          std::size_t iterations,
                          std::function<void()> fn,
                          std::size_t warmup) {
    results_.push_back(run(bench_name, iterations, std::move(fn), warmup));
}

void BenchmarkSuite::print_summary() const {
    std::cout << "\n=== Benchmark Suite: " << name_ << " ===\n";
    print_table(results_);
}

} // namespace anlt
