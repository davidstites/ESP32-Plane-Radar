#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <string>

#include "setup_link/setup_link.h"

using setup_link::buildSetupUrl;
using setup_link::encodeQr;
using setup_link::QrCode;
using setup_link::handleLocationRequest;
using setup_link::kTokenLen;
using setup_link::LocationResult;
using setup_link::makeToken;
using setup_link::parseLatLon;
using setup_link::tokenMatches;
using setup_link::validLatLon;

namespace {

constexpr char kAlphabet[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
constexpr char kBase[] = "https://metarly.app/setup";

int g_save_calls = 0;
double g_saved_lat = 0.0;
double g_saved_lon = 0.0;
bool g_save_result = true;

bool fakeSave(double lat, double lon) {
  ++g_save_calls;
  g_saved_lat = lat;
  g_saved_lon = lon;
  return g_save_result;
}

class HandleLocationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    g_save_calls = 0;
    g_saved_lat = 0.0;
    g_saved_lon = 0.0;
    g_save_result = true;
  }
};

}  // namespace

TEST(TokenTest, UsesOnlyUnambiguousAlphabet) {
  char token[kTokenLen + 1];
  for (uint32_t i = 0; i < 1000; ++i) {
    makeToken(i * 2654435761u, token);
    ASSERT_EQ(strlen(token), kTokenLen);
    for (size_t c = 0; c < kTokenLen; ++c) {
      EXPECT_NE(strchr(kAlphabet, token[c]), nullptr) << token;
    }
  }
}

TEST(TokenTest, IsDeterministicAndDistinct) {
  char a[kTokenLen + 1];
  char b[kTokenLen + 1];
  makeToken(123456789u, a);
  makeToken(123456789u, b);
  EXPECT_STREQ(a, b);

  makeToken(0, a);
  EXPECT_STREQ(a, "222222");
  makeToken(1, a);
  EXPECT_STREQ(a, "222223");
  makeToken(31, a);
  EXPECT_STREQ(a, "222232");
}

TEST(TokenTest, MatchRejectsNullPrefixAndExtension) {
  EXPECT_TRUE(tokenMatches("ABCDEF", "ABCDEF"));
  EXPECT_FALSE(tokenMatches(nullptr, "ABCDEF"));
  EXPECT_FALSE(tokenMatches("ABCDEF", nullptr));
  EXPECT_FALSE(tokenMatches("ABCDEF", "ABC"));
  EXPECT_FALSE(tokenMatches("ABCDEF", "ABCDEFG"));
  EXPECT_FALSE(tokenMatches("ABCDEF", ""));
  EXPECT_FALSE(tokenMatches("ABCDEF", "ABCDEG"));
}

TEST(TokenTest, MatchIgnoresCaseOfTypedCode) {
  EXPECT_TRUE(tokenMatches("ABCDEF", "abcdef"));
  EXPECT_TRUE(tokenMatches("ABCDEF", "AbCdEf"));
  EXPECT_FALSE(tokenMatches("ABCDEF", "abcdeg"));
}

TEST(TokenTest, EmptyExpectedTokenNeverMatches) {
  EXPECT_FALSE(tokenMatches("", ""));
  EXPECT_FALSE(tokenMatches("", "ABCDEF"));
}

TEST(BuildUrlTest, FormatsQuery) {
  char buf[128];
  const size_t n = buildSetupUrl(buf, sizeof(buf), kBase, "192.168.1.50", "ABCDEF");
  EXPECT_EQ(n, strlen(buf));
  EXPECT_STREQ(buf,
               "https://metarly.app/setup?device=192.168.1.50&token=ABCDEF");
}

TEST(BuildUrlTest, FailsWhenTooSmall) {
  const char expected[] =
      "https://metarly.app/setup?device=192.168.1.50&token=ABCDEF";
  const size_t len = strlen(expected);
  char buf[128];

  memset(buf, 'x', sizeof(buf));
  EXPECT_EQ(buildSetupUrl(buf, len, kBase, "192.168.1.50", "ABCDEF"), 0u);
  EXPECT_EQ(buf[0], '\0');

  memset(buf, 'x', sizeof(buf));
  EXPECT_EQ(buildSetupUrl(buf, 0, kBase, "192.168.1.50", "ABCDEF"), 0u);
  EXPECT_EQ(buf[0], 'x');

  EXPECT_EQ(buildSetupUrl(buf, len + 1, kBase, "192.168.1.50", "ABCDEF"), len);
  EXPECT_STREQ(buf, expected);
}

TEST(BuildUrlTest, RejectsNullParts) {
  char buf[128];
  EXPECT_EQ(buildSetupUrl(buf, sizeof(buf), nullptr, "1.2.3.4", "ABCDEF"), 0u);
  EXPECT_EQ(buildSetupUrl(buf, sizeof(buf), kBase, nullptr, "ABCDEF"), 0u);
  EXPECT_EQ(buildSetupUrl(buf, sizeof(buf), kBase, "1.2.3.4", nullptr), 0u);
  EXPECT_EQ(buildSetupUrl(nullptr, 10, kBase, "1.2.3.4", "ABCDEF"), 0u);
}

TEST(ParseLatLonTest, AcceptsValidAndBoundaryValues) {
  double lat = 0.0;
  double lon = 0.0;
  ASSERT_TRUE(parseLatLon("37.7749", "-122.4194", &lat, &lon));
  EXPECT_DOUBLE_EQ(lat, 37.7749);
  EXPECT_DOUBLE_EQ(lon, -122.4194);
  EXPECT_TRUE(parseLatLon("0", "0", &lat, &lon));
  EXPECT_TRUE(parseLatLon("90", "180", &lat, &lon));
  EXPECT_TRUE(parseLatLon("-90", "-180", &lat, &lon));
}

TEST(ParseLatLonTest, RejectsBadInputAndLeavesOutputsUntouched) {
  struct Case {
    const char* lat;
    const char* lon;
  };
  const Case cases[] = {
      {nullptr, "0"},    {"0", nullptr},    {"", "0"},         {"0", ""},
      {"abc", "0"},      {"37.7abc", "0"},  {"nan", "0"},      {"inf", "0"},
      {"0", "nan"},      {"0", "-inf"},     {"91", "0"},       {"-91", "0"},
      {"0", "181"},      {"0", "-181"},     {"37.7749", "xyz"},
  };
  for (const Case& c : cases) {
    double lat = 111.0;
    double lon = 222.0;
    EXPECT_FALSE(parseLatLon(c.lat, c.lon, &lat, &lon))
        << (c.lat ? c.lat : "null") << "," << (c.lon ? c.lon : "null");
    EXPECT_EQ(lat, 111.0);
    EXPECT_EQ(lon, 222.0);
  }
}

TEST(ValidLatLonTest, ChecksRangeAndRejectsNonFinite) {
  EXPECT_TRUE(validLatLon(0.0, 0.0));
  EXPECT_TRUE(validLatLon(90.0, 180.0));
  EXPECT_TRUE(validLatLon(-90.0, -180.0));
  EXPECT_FALSE(validLatLon(90.1, 0.0));
  EXPECT_FALSE(validLatLon(0.0, -180.1));
  EXPECT_FALSE(validLatLon(std::nan(""), 0.0));
  EXPECT_FALSE(validLatLon(0.0, std::nan("")));
}

TEST_F(HandleLocationTest, SavesValidRequest) {
  EXPECT_EQ(handleLocationRequest("ABCDEF", "ABCDEF", "37.7749", "-122.4194",
                                  fakeSave),
            LocationResult::kSaved);
  EXPECT_EQ(g_save_calls, 1);
  EXPECT_DOUBLE_EQ(g_saved_lat, 37.7749);
  EXPECT_DOUBLE_EQ(g_saved_lon, -122.4194);
}

TEST_F(HandleLocationTest, WrongTokenSavesNothingEvenWithBadCoordinates) {
  EXPECT_EQ(handleLocationRequest("ABCDEF", "ZZZZZZ", "37.7749", "-122.4194",
                                  fakeSave),
            LocationResult::kBadToken);
  EXPECT_EQ(handleLocationRequest("ABCDEF", "ZZZZZZ", "bad", "bad", fakeSave),
            LocationResult::kBadToken);
  EXPECT_EQ(handleLocationRequest("ABCDEF", nullptr, "37.7749", "-122.4194",
                                  fakeSave),
            LocationResult::kBadToken);
  EXPECT_EQ(g_save_calls, 0);
}

TEST_F(HandleLocationTest, BadCoordinatesSaveNothing) {
  EXPECT_EQ(handleLocationRequest("ABCDEF", "ABCDEF", "91", "0", fakeSave),
            LocationResult::kBadCoordinates);
  EXPECT_EQ(handleLocationRequest("ABCDEF", "ABCDEF", nullptr, nullptr, fakeSave),
            LocationResult::kBadCoordinates);
  EXPECT_EQ(g_save_calls, 0);
}

TEST_F(HandleLocationTest, ReportsSaveFailure) {
  g_save_result = false;
  EXPECT_EQ(handleLocationRequest("ABCDEF", "ABCDEF", "37.7749", "-122.4194",
                                  fakeSave),
            LocationResult::kSaveFailed);
  EXPECT_EQ(g_save_calls, 1);
}

TEST(QrTest, EncodesShortTextAtVersion1) {
  QrCode qr;
  ASSERT_TRUE(encodeQr("HI", &qr));
  EXPECT_EQ(qr.size, 21);
}

TEST(QrTest, SelectsLargerVersionForLongerText) {
  QrCode qr;
  ASSERT_TRUE(encodeQr("https://metarly.app/setup?device=192.168.1.50&token=ABCDEF", &qr));
  EXPECT_EQ(qr.size, 33);
}

TEST(QrTest, AcceptsLongestSupportedPayload) {
  QrCode qr;
  const std::string longest(106, 'a');
  ASSERT_TRUE(encodeQr(longest.c_str(), &qr));
  EXPECT_EQ(qr.size, 37);
}

// Matrix verified to decode back to the URL with an independent decoder (OpenCV).
TEST(QrTest, MatchesVerifiedMatrixForSetupUrl) {
  static const char* const kExpected[] = {
    "#######.#....##.#...#..#..#######",
    "#.....#.#..#####...##.#.#.#.....#",
    "#.###.#.##..#.#..#####.##.#.###.#",
    "#.###.#.###.#...#..#...#..#.###.#",
    "#.###.#..##.#.......#.##..#.###.#",
    "#.....#.###.......##.##...#.....#",
    "#######.#.#.#.#.#.#.#.#.#.#######",
    "..........##.#...#...##.#........",
    "##..###...#.##...###.###...#.####",
    "....#..##....##.#.#.##.###....#..",
    "####.##..##....#..#..###.##.#.##.",
    "...#.#.###..#.##.#.###.#..##.#...",
    ".#....#....#.##..#..###...####.##",
    "..#..#..###.#...#...#..#.#.#.....",
    ".#..#.##.#.########...###..#####.",
    "#..#...#...#.#.#.#...#.###...#...",
    ".....##..#..##...#...##...#.#...#",
    "#...##..###..##.###.#..#...#.#...",
    "##.####..##....#..#..#.#...#..##.",
    ".###....###.#.#.######.##..###...",
    "##....#..###.##..#.#.#....###...#",
    "####....#.#.#...###.####...#.#...",
    "....###.##.######...##.##...#.##.",
    "...###...#.#.#..###..#.##..##....",
    "##.#..#.###.##...#.#.#..#####..#.",
    "........##...##.#.#.#.#.#...##.#.",
    "#######..#.....#.##..##.#.#.#..#.",
    "#.....#.#...#.########..#...#...#",
    "#.###.#.####.##..#..##..#####..#.",
    "#.###.#..#..#...###.####.##.###..",
    "#.###.#..#.#####..#..###..#.#....",
    "#.....#.##.#.#.#.#.#.#..#####....",
    "#######.#.#.##.#####.#.##..#.#..#",
  };
  QrCode qr;
  ASSERT_TRUE(encodeQr("https://metarly.app/setup?device=192.168.1.50&token=ABCDEF", &qr));
  ASSERT_EQ(qr.size, 33);
  for (int y = 0; y < qr.size; ++y) {
    for (int x = 0; x < qr.size; ++x) {
      EXPECT_EQ(qr.module(static_cast<uint8_t>(x), static_cast<uint8_t>(y)),
                kExpected[y][x] == '#')
          << "module " << x << "," << y;
    }
  }
}

TEST(QrTest, FinderPatternCorners) {
  QrCode qr;
  ASSERT_TRUE(encodeQr("HI", &qr));
  EXPECT_TRUE(qr.module(0, 0));
  EXPECT_TRUE(qr.module(6, 0));
  EXPECT_TRUE(qr.module(0, 6));
  EXPECT_TRUE(qr.module(qr.size - 1, 0));
  EXPECT_TRUE(qr.module(0, qr.size - 1));
  EXPECT_FALSE(qr.module(7, 7));
  EXPECT_FALSE(qr.module(1, 1));
}

TEST(QrTest, RejectsTooLongNullAndEmpty) {
  QrCode qr;
  const std::string too_long(200, 'a');
  EXPECT_FALSE(encodeQr(too_long.c_str(), &qr));
  EXPECT_EQ(qr.size, 0);
  EXPECT_FALSE(encodeQr(nullptr, &qr));
  EXPECT_EQ(qr.size, 0);
  EXPECT_FALSE(encodeQr("", &qr));
  EXPECT_EQ(qr.size, 0);
}

TEST(QrTest, ModuleOutOfRangeIsLight) {
  QrCode qr;
  EXPECT_FALSE(qr.module(0, 0));
  ASSERT_TRUE(encodeQr("HI", &qr));
  EXPECT_FALSE(qr.module(qr.size, 0));
  EXPECT_FALSE(qr.module(0, qr.size));
  EXPECT_FALSE(qr.module(255, 255));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
