// analytics/tests/test_benchmark.cpp
#include "analytics/benchmark.hpp"
#include <gtest/gtest.h>
#include <atomic>

using namespace anlt;

TEST(Benchmark, RunReturnsResult) {
    auto r = run("NoOp", 100, []{ /* no-op */ }, 10);
    EXPECT_EQ(r.name, "NoOp");
    EXPECT_EQ(r.iterations, 100u);
    EXPECT_GT(r.latency_ns.n, 0u);
    EXPECT_GE(r.latency_ns.mean, 0.0);
}

TEST(Benchmark, MeasuresWork) {
    // A tiny busy-spin so elapsed time > 0
    volatile int x = 0;
    auto r = run("SpinOne", 500, [&]{ for (int i=0;i<100;++i) x+=i; }, 10);
    EXPECT_GT(r.latency_ns.mean, 0.0);
    EXPECT_GT(r.throughput_ops_per_sec, 0.0);
}

TEST(Benchmark, P99GeqMean) {
    auto r = run("Timing", 1000, []{ volatile double d = 3.14159 * 2.71828; (void)d; }, 50);
    EXPECT_GE(r.latency_ns.p99, r.latency_ns.mean);
}

TEST(Benchmark, SuiteCollects) {
    BenchmarkSuite suite("TestSuite");
    suite.add("A", 100, []{ volatile int x = 1+1; (void)x; });
    suite.add("B", 100, []{ volatile int x = 2+2; (void)x; });
    EXPECT_EQ(suite.results().size(), 2u);
    EXPECT_EQ(suite.name(), "TestSuite");
}

TEST(Benchmark, WarmupNotCounted) {
    // Warmup should not affect result iteration count
    auto r = run("W", 200, []{ }, /*warmup=*/1000);
    EXPECT_EQ(r.iterations, 200u);
    EXPECT_EQ(r.latency_ns.n, 200u);
}
