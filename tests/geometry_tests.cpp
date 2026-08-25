#include "laplace/geometry.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include <gtest/gtest.h>

namespace {

laplace_point4d Point(double x, double y, double z, double m) {
    return laplace_point4d{{x, y, z, m}};
}

std::uint64_t Bits(double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

laplace_geometry_summary Finish(const laplace_geometry_accumulator& accumulator) {
    laplace_geometry_summary summary{};
    EXPECT_EQ(laplace_geometry_accumulator_finish(&accumulator, &summary),
              LAPLACE_GEOMETRY_OK);
    return summary;
}

TEST(GeometryAccumulator, EmptyStateIsExplicit) {
    laplace_geometry_accumulator accumulator{};
    laplace_geometry_summary summary{};
    laplace_geometry_accumulator_init(&accumulator);
    EXPECT_EQ(accumulator.logical_count, 0u);
    EXPECT_EQ(laplace_geometry_accumulator_finish(&accumulator, &summary),
              LAPLACE_GEOMETRY_EMPTY);
}

TEST(GeometryCentroid, SinglePointPreservesItsS3CoordinateAndRadius) {
    laplace_geometry_accumulator accumulator{};
    const auto point = Point(1.0, 0.0, 0.0, 0.0);
    laplace_geometry_accumulator_init(&accumulator);
    ASSERT_EQ(laplace_geometry_accumulator_add(&accumulator, &point, 1u),
              LAPLACE_GEOMETRY_OK);
    const auto summary = Finish(accumulator);
    EXPECT_EQ(summary.logical_count, 1u);
    EXPECT_EQ(summary.centroid.component[0], 1.0);
    EXPECT_EQ(summary.centroid.component[1], 0.0);
    EXPECT_EQ(summary.centroid.component[2], 0.0);
    EXPECT_EQ(summary.centroid.component[3], 0.0);
    EXPECT_EQ(summary.radius, 1.0);
}

TEST(GeometryCentroid, CompositeUsesTheFourDimensionalArithmeticCentroid) {
    laplace_geometry_accumulator accumulator{};
    const auto x = Point(1.0, 0.0, 0.0, 0.0);
    const auto y = Point(0.0, 1.0, 0.0, 0.0);
    laplace_geometry_accumulator_init(&accumulator);
    ASSERT_EQ(laplace_geometry_accumulator_add(&accumulator, &x, 1u),
              LAPLACE_GEOMETRY_OK);
    ASSERT_EQ(laplace_geometry_accumulator_add(&accumulator, &y, 1u),
              LAPLACE_GEOMETRY_OK);
    const auto summary = Finish(accumulator);
    EXPECT_EQ(summary.centroid.component[0], 0.5);
    EXPECT_EQ(summary.centroid.component[1], 0.5);
    EXPECT_EQ(summary.centroid.component[2], 0.0);
    EXPECT_EQ(summary.centroid.component[3], 0.0);
    EXPECT_DOUBLE_EQ(summary.radius, std::sqrt(0.5));
    EXPECT_LT(summary.radius, 1.0);
}

TEST(GeometryCentroid, RunMultiplicityMatchesExpandedInputBitForBit) {
    constexpr std::uint64_t count = 100000u;
    const auto sky_blue = Point(0.25, 0.5, 0.75, 0.125);
    laplace_geometry_accumulator run{};
    laplace_geometry_accumulator expanded{};
    laplace_geometry_accumulator_init(&run);
    laplace_geometry_accumulator_init(&expanded);
    ASSERT_EQ(laplace_geometry_accumulator_add(&run, &sky_blue, count),
              LAPLACE_GEOMETRY_OK);
    for (std::uint64_t index = 0; index < count; ++index) {
        ASSERT_EQ(laplace_geometry_accumulator_add(&expanded, &sky_blue, 1u),
                  LAPLACE_GEOMETRY_OK);
    }
    const auto run_summary = Finish(run);
    const auto expanded_summary = Finish(expanded);
    for (std::size_t component = 0; component < LAPLACE_GEOMETRY_COMPONENTS; ++component) {
        EXPECT_EQ(Bits(run_summary.centroid.component[component]),
                  Bits(expanded_summary.centroid.component[component]));
    }
    EXPECT_EQ(Bits(run_summary.radius), Bits(expanded_summary.radius));
    EXPECT_EQ(run_summary.logical_count, expanded_summary.logical_count);
}

TEST(GeometryCentroid, InputAndMergeOrderCannotChangeCentroidBits) {
    const std::array<laplace_point4d, 6> points{{
        Point(1.0, 0.0, 0.0, 0.0),
        Point(-1.0, 0.0, 0.0, 0.0),
        Point(0.1, -0.3, 0.5, -0.7),
        Point(-0.1, 0.3, -0.5, 0.7),
        Point(std::ldexp(1.0, -1000), 0.25, -0.25, 0.0),
        Point(0.0, -0.25, 0.25, std::ldexp(1.0, -1000))
    }};
    laplace_geometry_accumulator forward{};
    laplace_geometry_accumulator reverse{};
    laplace_geometry_accumulator left{};
    laplace_geometry_accumulator right{};
    laplace_geometry_accumulator merged{};
    laplace_geometry_accumulator_init(&forward);
    laplace_geometry_accumulator_init(&reverse);
    laplace_geometry_accumulator_init(&left);
    laplace_geometry_accumulator_init(&right);
    laplace_geometry_accumulator_init(&merged);

    for (std::size_t index = 0; index < points.size(); ++index) {
        ASSERT_EQ(laplace_geometry_accumulator_add(&forward, &points[index], 1u),
                  LAPLACE_GEOMETRY_OK);
        ASSERT_EQ(laplace_geometry_accumulator_add(
                      &reverse, &points[points.size() - index - 1u], 1u),
                  LAPLACE_GEOMETRY_OK);
        ASSERT_EQ(laplace_geometry_accumulator_add(
                      index < 3u ? &left : &right, &points[index], 1u),
                  LAPLACE_GEOMETRY_OK);
    }
    ASSERT_EQ(laplace_geometry_accumulator_merge(&merged, &left),
              LAPLACE_GEOMETRY_OK);
    ASSERT_EQ(laplace_geometry_accumulator_merge(&merged, &right),
              LAPLACE_GEOMETRY_OK);

    const auto forward_summary = Finish(forward);
    const auto reverse_summary = Finish(reverse);
    const auto merged_summary = Finish(merged);
    for (std::size_t component = 0; component < LAPLACE_GEOMETRY_COMPONENTS; ++component) {
        EXPECT_EQ(Bits(forward_summary.centroid.component[component]),
                  Bits(reverse_summary.centroid.component[component]));
        EXPECT_EQ(Bits(forward_summary.centroid.component[component]),
                  Bits(merged_summary.centroid.component[component]));
    }
    EXPECT_EQ(Bits(forward_summary.radius), Bits(reverse_summary.radius));
    EXPECT_EQ(Bits(forward_summary.radius), Bits(merged_summary.radius));
}

TEST(GeometryCentroid, PreservesTinyResidualAfterLargeCancellation) {
    const double residual = std::ldexp(1.0, -1000);
    const std::array<laplace_point4d, 3> points{{
        Point(1.0, 0.0, 0.0, 0.0),
        Point(residual, 0.0, 0.0, 0.0),
        Point(-1.0, 0.0, 0.0, 0.0)
    }};
    laplace_geometry_accumulator accumulator{};
    laplace_geometry_accumulator_init(&accumulator);
    for (const auto& point : points) {
        ASSERT_EQ(laplace_geometry_accumulator_add(&accumulator, &point, 1u),
                  LAPLACE_GEOMETRY_OK);
    }
    const auto summary = Finish(accumulator);
    EXPECT_EQ(Bits(summary.centroid.component[0]), Bits(residual / 3.0));
}

TEST(GeometryCentroid, RoundsSubnormalTiesToEven) {
    const double minimum = std::numeric_limits<double>::denorm_min();
    const auto tiny = Point(minimum, 0.0, 0.0, 0.0);
    const auto zero = Point(0.0, 0.0, 0.0, 0.0);
    laplace_geometry_accumulator accumulator{};
    laplace_geometry_accumulator_init(&accumulator);
    ASSERT_EQ(laplace_geometry_accumulator_add(&accumulator, &tiny, 1u),
              LAPLACE_GEOMETRY_OK);
    ASSERT_EQ(laplace_geometry_accumulator_add(&accumulator, &zero, 1u),
              LAPLACE_GEOMETRY_OK);
    const auto summary = Finish(accumulator);
    EXPECT_EQ(Bits(summary.centroid.component[0]), Bits(0.0));
}

TEST(GeometryAccumulator, RejectsInvalidInputWithoutPartialMutation) {
    const auto valid = Point(0.25, 0.0, 0.0, 0.0);
    const auto invalid = Point(
        std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0, 0.0);
    laplace_geometry_accumulator accumulator{};
    laplace_geometry_accumulator_init(&accumulator);
    ASSERT_EQ(laplace_geometry_accumulator_add(&accumulator, &valid, 1u),
              LAPLACE_GEOMETRY_OK);
    const auto before = accumulator;
    EXPECT_EQ(laplace_geometry_accumulator_add(&accumulator, &invalid, 1u),
              LAPLACE_GEOMETRY_COORDINATE_OUT_OF_RANGE);
    EXPECT_EQ(laplace_geometry_accumulator_add(&accumulator, &valid, 0u),
              LAPLACE_GEOMETRY_ZERO_MULTIPLICITY);
    EXPECT_EQ(std::memcmp(&before, &accumulator, sizeof(before)), 0);
}

TEST(GeometryAccumulator, DetectsLogicalCountOverflow) {
    const auto point = Point(0.5, 0.0, 0.0, 0.0);
    laplace_geometry_accumulator accumulator{};
    laplace_geometry_accumulator_init(&accumulator);
    ASSERT_EQ(laplace_geometry_accumulator_add(
                  &accumulator, &point, std::numeric_limits<std::uint64_t>::max()),
              LAPLACE_GEOMETRY_OK);
    EXPECT_EQ(laplace_geometry_accumulator_add(&accumulator, &point, 1u),
              LAPLACE_GEOMETRY_COUNT_OVERFLOW);
    const auto summary = Finish(accumulator);
    EXPECT_EQ(summary.centroid.component[0], 0.5);
    EXPECT_EQ(summary.logical_count, std::numeric_limits<std::uint64_t>::max());
}

}  // namespace
