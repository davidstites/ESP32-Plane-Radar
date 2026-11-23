#include <gtest/gtest.h>

#include <cmath>

#include "radar_geo/radar_geo.h"

using radar_geo::clipToDisc;
using radar_geo::deadReckon;
using radar_geo::LatLon;
using radar_geo::distSq;
using radar_geo::kDegToRad;
using radar_geo::kKmPerDeg;
using radar_geo::OffsetKm;
using radar_geo::offsetKm;
using radar_geo::ScreenPoint;
using radar_geo::toScreen;

namespace {

constexpr double kAmsLat = 52.3676;
constexpr double kAmsLon = 4.9041;

}  // namespace

TEST(RadarGeoTest, DegToRadConvertsHalfTurn) {
  EXPECT_FLOAT_EQ(180.0f * kDegToRad, 3.14159265f);
}

TEST(RadarGeoTest, CenterHasZeroOffset) {
  const OffsetKm o = offsetKm(kAmsLat, kAmsLon, static_cast<float>(kAmsLat),
                              static_cast<float>(kAmsLon));
  EXPECT_NEAR(o.dx, 0.0f, 1e-3f);
  EXPECT_NEAR(o.dy, 0.0f, 1e-3f);
  EXPECT_NEAR(o.dist, 0.0f, 1e-3f);
}

TEST(RadarGeoTest, NorthIsPositiveY) {
  const OffsetKm o = offsetKm(0.0, 0.0, 1.0f, 0.0f);
  EXPECT_FLOAT_EQ(o.dx, 0.0f);
  EXPECT_FLOAT_EQ(o.dy, kKmPerDeg);
  EXPECT_FLOAT_EQ(o.dist, kKmPerDeg);
}

TEST(RadarGeoTest, SouthWestIsNegative) {
  const OffsetKm o = offsetKm(0.0, 0.0, -1.0f, -1.0f);
  EXPECT_FLOAT_EQ(o.dx, -kKmPerDeg);
  EXPECT_FLOAT_EQ(o.dy, -kKmPerDeg);
}

TEST(RadarGeoTest, EastShrinksWithCenterLatitude) {
  // At 60° a degree of longitude is half as wide as at the equator.
  const OffsetKm o = offsetKm(60.0, 10.0, 60.0f, 11.0f);
  EXPECT_NEAR(o.dx, kKmPerDeg * 0.5f, 1e-3f);
  EXPECT_FLOAT_EQ(o.dy, 0.0f);
}

TEST(RadarGeoTest, DistanceIsHypotenuse) {
  const OffsetKm o = offsetKm(0.0, 0.0, 4.0f / kKmPerDeg, 3.0f / kKmPerDeg);
  EXPECT_NEAR(o.dist, 5.0f, 1e-4f);
}

TEST(RadarGeoTest, OffsetUsesDoublePrecisionDifference) {
  // A ~10 m offset survives even though the point is a float near 52°.
  const float lat = static_cast<float>(kAmsLat + 0.0001);
  const OffsetKm o = offsetKm(kAmsLat, kAmsLon, lat, static_cast<float>(kAmsLon));
  EXPECT_NEAR(o.dy, static_cast<float>((static_cast<double>(lat) - kAmsLat) * kKmPerDeg),
              1e-6f);
}

TEST(RadarGeoTest, CenterMapsToScreenCenter) {
  const ScreenPoint p = toScreen({0.0f, 0.0f, 0.0f}, 10.0f, 100, 120, 120);
  EXPECT_EQ(p.x, 120);
  EXPECT_EQ(p.y, 120);
}

TEST(RadarGeoTest, OuterKmMapsToOuterRadiusWithNorthUp) {
  EXPECT_EQ(toScreen({10.0f, 0.0f, 10.0f}, 10.0f, 100, 120, 120).x, 220);
  EXPECT_EQ(toScreen({0.0f, 10.0f, 10.0f}, 10.0f, 100, 120, 120).y, 20);
  EXPECT_EQ(toScreen({-5.0f, -5.0f, 7.1f}, 10.0f, 100, 120, 120).x, 70);
  EXPECT_EQ(toScreen({-5.0f, -5.0f, 7.1f}, 10.0f, 100, 120, 120).y, 170);
}

TEST(RadarGeoTest, ScreenRoundsHalfAwayFromZero) {
  // 0.25 km at 2 px/km = 0.5 px: rounds to 1 east and to -1 (screen up) north.
  const ScreenPoint p = toScreen({0.25f, 0.25f, 0.35f}, 50.0f, 100, 0, 0);
  EXPECT_EQ(p.x, 1);
  EXPECT_EQ(p.y, -1);
  EXPECT_EQ(toScreen({-0.25f, 0.0f, 0.25f}, 50.0f, 100, 0, 0).x, -1);
}

TEST(RadarGeoTest, DistSqFromCenter) {
  EXPECT_EQ(distSq(3, 4, 0, 0), 25);
  EXPECT_EQ(distSq(120, 120, 120, 120), 0);
  EXPECT_EQ(distSq(117, 124, 120, 120), 25);
}

TEST(RadarGeoTest, ClipKeepsPointInsideDisc) {
  const ScreenPoint p = clipToDisc({0, 0}, {60, -80}, 0, 0, 100);
  EXPECT_EQ(p.x, 60);
  EXPECT_EQ(p.y, -80);
}

TEST(RadarGeoTest, ClipPullsPointBackToDiscEdge) {
  const ScreenPoint p = clipToDisc({0, 0}, {200, 0}, 0, 0, 100);
  EXPECT_EQ(p.x, 100);
  EXPECT_EQ(p.y, 0);
}

TEST(RadarGeoTest, ClipStepsInFivePercentIncrements) {
  // sqrt(2)/2 of the way is the edge; the last 5% step inside is 70%.
  const ScreenPoint p = clipToDisc({120, 120}, {20, 20}, 120, 120, 100);
  EXPECT_EQ(p.x, 50);
  EXPECT_EQ(p.y, 50);
}

TEST(RadarGeoTest, ClipFallsBackToStartWhenNoStepFits) {
  const ScreenPoint p = clipToDisc({100, 0}, {300, 0}, 0, 0, 100);
  EXPECT_EQ(p.x, 100);
  EXPECT_EQ(p.y, 0);
}

TEST(RadarGeoTest, RectsOverlapWhenTheyShareArea) {
  const radar_geo::Rect a = {10, 10, 20, 10};
  EXPECT_TRUE(radar_geo::rectsOverlap(a, {25, 15, 20, 10}));  // corner
  EXPECT_TRUE(radar_geo::rectsOverlap(a, {12, 12, 4, 4}));    // contained
  EXPECT_TRUE(radar_geo::rectsOverlap({12, 12, 4, 4}, a));    // symmetric
}

TEST(RadarGeoTest, RectsThatOnlyTouchDoNotOverlap) {
  const radar_geo::Rect a = {10, 10, 20, 10};
  EXPECT_FALSE(radar_geo::rectsOverlap(a, {30, 10, 5, 10}));  // right edge
  EXPECT_FALSE(radar_geo::rectsOverlap(a, {10, 20, 20, 5}));  // bottom edge
  EXPECT_FALSE(radar_geo::rectsOverlap(a, {0, 10, 10, 10}));  // left edge
  EXPECT_FALSE(radar_geo::rectsOverlap(a, {10, 0, 20, 10}));  // top edge
  EXPECT_FALSE(radar_geo::rectsOverlap(a, {100, 100, 5, 5}));  // far away
}

TEST(RadarGeoTest, FirstClearCandidatePrefersTheEarliestFreeOne) {
  const radar_geo::Rect placed[] = {{0, 0, 10, 10}, {20, 0, 10, 10}};
  const radar_geo::Rect candidates[] = {{5, 5, 10, 10}, {22, 2, 4, 4}, {40, 40, 4, 4}, {60, 60, 4, 4}};
  EXPECT_EQ(radar_geo::firstClearCandidate(candidates, 4, placed, 2), 2);
}

TEST(RadarGeoTest, FirstClearCandidateIsTheFirstWhenNothingIsPlaced) {
  const radar_geo::Rect candidates[] = {{5, 5, 10, 10}, {40, 40, 4, 4}};
  EXPECT_EQ(radar_geo::firstClearCandidate(candidates, 2, nullptr, 0), 0);
}

TEST(RadarGeoTest, FirstClearCandidateReportsMinusOneWhenAllCollide) {
  const radar_geo::Rect placed[] = {{0, 0, 50, 50}};
  const radar_geo::Rect candidates[] = {{5, 5, 10, 10}, {20, 20, 4, 4}};
  EXPECT_EQ(radar_geo::firstClearCandidate(candidates, 2, placed, 1), -1);
  EXPECT_EQ(radar_geo::firstClearCandidate(candidates, 0, placed, 1), -1);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(RadarGeoTest, DeadReckonStandingStillOrZeroElapsedStays) {
  const LatLon still = deadReckon(52.0f, 4.0f, 90.0f, 0.0f, 60000);
  EXPECT_FLOAT_EQ(still.lat, 52.0f);
  EXPECT_FLOAT_EQ(still.lon, 4.0f);
  const LatLon fresh = deadReckon(52.0f, 4.0f, 90.0f, 300.0f, 0);
  EXPECT_FLOAT_EQ(fresh.lat, 52.0f);
  EXPECT_FLOAT_EQ(fresh.lon, 4.0f);
}

TEST(RadarGeoTest, DeadReckonNorthMovesLatitudeOnly) {
  // One hour at 60 kt is 111.12 km, just over a degree of latitude.
  const LatLon p = deadReckon(10.0f, 20.0f, 0.0f, 60.0f, 3600000);
  EXPECT_NEAR(p.lat, 10.0f + 60.0f * 1.852f / kKmPerDeg, 1e-4f);
  EXPECT_NEAR(p.lon, 20.0f, 1e-4f);
}

TEST(RadarGeoTest, DeadReckonEastWidensLongitudeByInverseCosLat) {
  const float lat = 60.0f;  // cos = 0.5, so a km of eastward flight is 2x the degrees
  const LatLon p = deadReckon(lat, 5.0f, 90.0f, 60.0f, 3600000);
  const float km = 60.0f * 1.852f;
  EXPECT_NEAR(p.lon, 5.0f + km / (kKmPerDeg * 0.5f), 1e-3f);
  EXPECT_NEAR(p.lat, lat, 1e-3f);
}

TEST(RadarGeoTest, DeadReckonDistanceRoundTripsThroughOffsetKm) {
  const float lat = static_cast<float>(kAmsLat);
  const float lon = static_cast<float>(kAmsLon);
  const LatLon p = deadReckon(lat, lon, 45.0f, 120.0f, 30000);  // 120 kt for 30 s
  const OffsetKm o = offsetKm(kAmsLat, kAmsLon, p.lat, p.lon);
  EXPECT_NEAR(o.dist, 120.0f * 1.852f * (30.0f / 3600.0f), 0.01f);
  EXPECT_NEAR(o.dx, o.dy, 0.01f);  // 45 degrees: equal east and north
}

TEST(RadarGeoTest, SameViewComparesCenterAndRange) {
  const radar_geo::ViewKey base = {37.0, -121.0, 10.0f};
  EXPECT_TRUE(radar_geo::sameView(base, {37.0, -121.0, 10.0f}));
  EXPECT_FALSE(radar_geo::sameView(base, {37.1, -121.0, 10.0f}));
  EXPECT_FALSE(radar_geo::sameView(base, {37.0, -121.1, 10.0f}));
  EXPECT_FALSE(radar_geo::sameView(base, {37.0, -121.0, 15.0f}));
}

TEST(RadarGeoTest, AirportDisplayIdentDropsLeadingKFromFourLetterCodes) {
  EXPECT_STREQ(radar_geo::airportDisplayIdent("KSJC"), "SJC");
  EXPECT_STREQ(radar_geo::airportDisplayIdent("KE16"), "E16");
  EXPECT_STREQ(radar_geo::airportDisplayIdent("RKSI"), "RKSI");
  EXPECT_STREQ(radar_geo::airportDisplayIdent("EGLL"), "EGLL");
  EXPECT_STREQ(radar_geo::airportDisplayIdent("KSJ"), "KSJ");
  EXPECT_STREQ(radar_geo::airportDisplayIdent("K"), "K");
  EXPECT_STREQ(radar_geo::airportDisplayIdent("KABCD"), "KABCD");
}

TEST(RadarGeoTest, ParseRingRangeKmConvertsMilesAndKm) {
  float km = 0.0f;
  ASSERT_TRUE(radar_geo::parseRingRangeKm("10", false, &km));
  EXPECT_FLOAT_EQ(km, 10.0f);
  ASSERT_TRUE(radar_geo::parseRingRangeKm("10", true, &km));
  EXPECT_NEAR(km, 16.09344f, 1e-4f);
  ASSERT_TRUE(radar_geo::parseRingRangeKm(" 7.5 ", true, &km));
  EXPECT_NEAR(km, 7.5f * radar_geo::kKmPerMile, 1e-4f);
}

TEST(RadarGeoTest, ParseRingRangeKmClampsToOneUnitAndTwentyFiveMiles) {
  float km = 0.0f;
  const float max_km = 25.0f * radar_geo::kKmPerMile;
  ASSERT_TRUE(radar_geo::parseRingRangeKm("25", true, &km));
  EXPECT_NEAR(km, max_km, 1e-4f);
  ASSERT_TRUE(radar_geo::parseRingRangeKm("250", true, &km));
  EXPECT_NEAR(km, max_km, 1e-4f);
  ASSERT_TRUE(radar_geo::parseRingRangeKm("100", false, &km));
  EXPECT_NEAR(km, max_km, 1e-4f);
  ASSERT_TRUE(radar_geo::parseRingRangeKm("0.2", true, &km));
  EXPECT_NEAR(km, radar_geo::kKmPerMile, 1e-4f);
  ASSERT_TRUE(radar_geo::parseRingRangeKm("-5", false, &km));
  EXPECT_FLOAT_EQ(km, 1.0f);
}

TEST(RadarGeoTest, ParseRingRangeKmRejectsNonNumbers) {
  float km = 42.0f;
  EXPECT_FALSE(radar_geo::parseRingRangeKm(nullptr, true, &km));
  EXPECT_FALSE(radar_geo::parseRingRangeKm("", true, &km));
  EXPECT_FALSE(radar_geo::parseRingRangeKm("abc", true, &km));
  EXPECT_FALSE(radar_geo::parseRingRangeKm("12abc", true, &km));
  EXPECT_FALSE(radar_geo::parseRingRangeKm("nan", true, &km));
  EXPECT_FALSE(radar_geo::parseRingRangeKm("inf", true, &km));
  EXPECT_FLOAT_EQ(km, 42.0f);
}

TEST(RadarGeoTest, NextRingKmStepsToLargerPresetThenWraps) {
  const float presets[] = {5.0f, 10.0f, 15.0f, 25.0f};
  EXPECT_FLOAT_EQ(radar_geo::nextRingKm(5.0f, presets, 4), 10.0f);
  EXPECT_FLOAT_EQ(radar_geo::nextRingKm(12.0f, presets, 4), 15.0f);
  EXPECT_FLOAT_EQ(radar_geo::nextRingKm(15.0f, presets, 4), 25.0f);
  EXPECT_FLOAT_EQ(radar_geo::nextRingKm(25.0f, presets, 4), 5.0f);
  EXPECT_FLOAT_EQ(radar_geo::nextRingKm(33.0f, presets, 4), 5.0f);
  EXPECT_FLOAT_EQ(radar_geo::nextRingKm(1.0f, presets, 4), 5.0f);
}
