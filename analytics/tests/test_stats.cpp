// analytics/tests/test_stats.cpp
#include "analytics/stats.hpp"
#include <gtest/gtest.h>
#include <cmath>

using namespace anlt;

TEST(Stats, MeanEmpty)      { EXPECT_NEAR(mean({}), 0.0, 1e-9); }
TEST(Stats, MeanSingle)     { EXPECT_NEAR(mean(std::vector<double>{5.0}), 5.0, 1e-9); }
TEST(Stats, MeanKnown)      {
    std::vector<double> v{1,2,3,4,5};
    EXPECT_NEAR(mean(v), 3.0, 1e-9);
}
TEST(Stats, VarianceConst)  {
    std::vector<double> v{7,7,7,7};
    EXPECT_NEAR(variance(v), 0.0, 1e-9);
}
TEST(Stats, StddevKnown)    {
    // population stddev of {2,4,4,4,5,5,7,9} = 2
    std::vector<double> v{2,4,4,4,5,5,7,9};
    EXPECT_NEAR(stddev(v), 2.0, 1e-6);
}
TEST(Stats, PercentileMedian) {
    std::vector<double> v{1,2,3,4,5};
    EXPECT_NEAR(percentile(v, 0.5), 3.0, 1e-6);
}
TEST(Stats, PercentileMin) {
    std::vector<double> v{1,2,3,4,5};
    EXPECT_NEAR(percentile(v, 0.0), 1.0, 1e-6);
}
TEST(Stats, PercentileMax) {
    std::vector<double> v{1,2,3,4,5};
    EXPECT_NEAR(percentile(v, 1.0), 5.0, 1e-6);
}
TEST(Stats, DescribeFields) {
    std::vector<double> v;
    for (int i = 1; i <= 100; ++i) v.push_back(i);
    auto d = describe(v);
    EXPECT_EQ(d.n, 100u);
    EXPECT_NEAR(d.min, 1.0, 1e-6);
    EXPECT_NEAR(d.max, 100.0, 1e-6);
    EXPECT_NEAR(d.mean, 50.5, 1e-6);
    EXPECT_GT(d.p99, d.p95);
    EXPECT_GT(d.p95, d.p90);
    EXPECT_GT(d.p90, d.p50);
}
TEST(Stats, GeoMean) {
    // geo_mean of {0.1, 0.1} = (1.1^2)^0.5 - 1 = 0.1
    std::vector<double> v{0.1, 0.1};
    EXPECT_NEAR(geo_mean(v), 0.1, 1e-6);
}
TEST(Stats, SkewnessSymmetric) {
    std::vector<double> v{-1, 0, 1};
    EXPECT_NEAR(skewness(v), 0.0, 1e-6);
}
TEST(Stats, KurtosisNormal) {
    // Normal distribution has excess kurtosis ~0; 4-pt uniform has negative
    std::vector<double> v{-3,-1,1,3};
    // Excess kurtosis of uniform 4-pt distribution
    EXPECT_LT(kurtosis(v), 0.0);  // platykurtic (thin tails)
}
