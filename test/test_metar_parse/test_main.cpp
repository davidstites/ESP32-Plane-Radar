#include <gtest/gtest.h>

#include <cstring>
#include <sstream>
#include <string>

#include "../support/fixture.h"
#include "metar_parse/metar_parser.h"

using metar_parse::Category;
using metar_parse::extractStations;
using metar_parse::kMaxStations;
using metar_parse::parseStations;
using metar_parse::Station;

namespace {

Category categoryOf(const Station* stations, size_t count, const char* icao) {
  for (size_t i = 0; i < count; ++i) {
    if (strcmp(stations[i].icao, icao) == 0) {
      return stations[i].category;
    }
  }
  return Category::kUnknown;
}

}  // namespace

TEST(MetarCategory, MapsEveryNoaaCategory) {
  EXPECT_EQ(metar_parse::categoryFromText("VFR"), Category::kVfr);
  EXPECT_EQ(metar_parse::categoryFromText("MVFR"), Category::kMvfr);
  EXPECT_EQ(metar_parse::categoryFromText("IFR"), Category::kIfr);
  EXPECT_EQ(metar_parse::categoryFromText("LIFR"), Category::kLifr);
}

TEST(MetarCategory, AnythingElseIsUnknown) {
  EXPECT_EQ(metar_parse::categoryFromText(nullptr), Category::kUnknown);
  EXPECT_EQ(metar_parse::categoryFromText(""), Category::kUnknown);
  EXPECT_EQ(metar_parse::categoryFromText("vfr"), Category::kUnknown);
  EXPECT_EQ(metar_parse::categoryFromText("VFRX"), Category::kUnknown);
}

TEST(MetarParse, FirstReportPerStationWins) {
  // The feed lists a station's newest report first: SFO got worse after the older VFR.
  const std::string json =
      R"([{"icaoId":"KSFO","fltCat":"IFR"},{"icaoId":"KOAK","fltCat":"VFR"},)"
      R"({"icaoId":"KSFO","fltCat":"VFR"}])";
  Station out[kMaxStations];
  const auto result = parseStations(json, out, kMaxStations);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(result.count, 2u);
  EXPECT_EQ(categoryOf(out, 2, "KSFO"), Category::kIfr);
  EXPECT_EQ(categoryOf(out, 2, "KOAK"), Category::kVfr);
}

TEST(MetarParse, MissingOrOddCategoryIsUnknown) {
  const std::string json =
      R"([{"icaoId":"KAAA"},{"icaoId":"KBBB","fltCat":null},{"icaoId":"KCCC","fltCat":"???"}])";
  Station out[kMaxStations];
  const auto result = parseStations(json, out, kMaxStations);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(result.count, 3u);
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(out[i].category, Category::kUnknown);
  }
}

TEST(MetarParse, SkipsEntriesWithoutAUsableId) {
  const std::string json =
      R"([{"fltCat":"VFR"},{"icaoId":"","fltCat":"VFR"},{"icaoId":"TOOLONG","fltCat":"VFR"},)"
      R"({"icaoId":"K1O2","fltCat":"MVFR"}])";
  Station out[kMaxStations];
  const auto result = parseStations(json, out, kMaxStations);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(result.count, 1u);
  EXPECT_STREQ(out[0].icao, "K1O2");
}

TEST(MetarParse, StopsAtCapacity) {
  const std::string json =
      R"([{"icaoId":"KAAA","fltCat":"VFR"},{"icaoId":"KBBB","fltCat":"VFR"},{"icaoId":"KCCC","fltCat":"VFR"}])";
  Station out[2];
  const auto result = parseStations(json, out, 2);
  ASSERT_TRUE(result.ok);
  EXPECT_EQ(result.count, 2u);
  EXPECT_STREQ(out[1].icao, "KBBB");
}

TEST(MetarParse, EmptyArrayIsSuccessWithNoStations) {
  Station out[kMaxStations];
  const auto result = parseStations(std::string("[]"), out, kMaxStations);
  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.count, 0u);
  EXPECT_EQ(result.error, nullptr);
}

TEST(MetarParse, RejectsNonArrayJson) {
  Station out[kMaxStations];
  const auto result = parseStations(std::string(R"({"error":"nope"})"), out, kMaxStations);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.count, 0u);
  EXPECT_NE(result.error, nullptr);
}

TEST(MetarParse, RejectsBrokenJson) {
  Station out[kMaxStations];
  const auto result = parseStations(std::string(R"([{"icaoId":"KSFO",)"), out, kMaxStations);
  EXPECT_FALSE(result.ok);
  EXPECT_NE(result.error, nullptr);
}

TEST(MetarParse, RejectsBrokenJsonFromAnLvalueString) {
  const std::string json = R"([{"icaoId":"KSFO",)";
  Station out[kMaxStations];
  const auto result = parseStations(json, out, kMaxStations);
  EXPECT_FALSE(result.ok);
  EXPECT_NE(result.error, nullptr);
}

TEST(MetarParse, RejectsBrokenJsonFromAStream) {
  std::istringstream stream(R"([{"icaoId":"KSFO",)");
  Station out[kMaxStations];
  const auto result = parseStations(stream, out, kMaxStations);
  EXPECT_FALSE(result.ok);
  EXPECT_NE(result.error, nullptr);
}

TEST(MetarParse, ReadsFromAStream) {
  std::istringstream stream(R"([{"icaoId":"KSFO","fltCat":"LIFR"}])");
  Station out[kMaxStations];
  const auto result = parseStations(stream, out, kMaxStations);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(result.count, 1u);
  EXPECT_EQ(out[0].category, Category::kLifr);
}

TEST(MetarParse, RecordedResponseGivesOneCategoryPerStation) {
  const std::string json = readFixture("metar_5_stations_3h.json");
  ASSERT_FALSE(json.empty());
  Station out[kMaxStations];
  const auto result = parseStations(json, out, kMaxStations);
  ASSERT_TRUE(result.ok);
  EXPECT_EQ(result.count, 5u);  // 20 reports across 5 stations
  EXPECT_NE(categoryOf(out, result.count, "KSFO"), Category::kUnknown);
  EXPECT_EQ(categoryOf(out, result.count, "KJFK"), Category::kMvfr);
  EXPECT_EQ(categoryOf(out, result.count, "KORD"), Category::kVfr);
}

TEST(MetarRequestPath, JoinsIdsWithCommas) {
  const char* ids[] = {"KSFO", "KOAK", "E16"};
  char path[128];
  ASSERT_TRUE(metar_parse::buildRequestPath(path, sizeof(path), ids, 3));
  EXPECT_STREQ(path, "/api/data/metar?ids=KSFO,KOAK,E16&format=json&hours=2");
}

TEST(MetarRequestPath, SingleId) {
  const char* ids[] = {"KSFO"};
  char path[128];
  ASSERT_TRUE(metar_parse::buildRequestPath(path, sizeof(path), ids, 1));
  EXPECT_STREQ(path, "/api/data/metar?ids=KSFO&format=json&hours=2");
}

TEST(MetarRequestPath, NoIdsFails) {
  char path[16] = "junk";
  EXPECT_FALSE(metar_parse::buildRequestPath(path, sizeof(path), nullptr, 0));
  EXPECT_STREQ(path, "");
}

TEST(MetarRequestPath, FailsAndEmptiesWhenItDoesNotFit) {
  const char* ids[] = {"KSFO", "KOAK"};
  char path[40];
  EXPECT_FALSE(metar_parse::buildRequestPath(path, sizeof(path), ids, 2));
  EXPECT_STREQ(path, "");
}

TEST(MetarRequestPath, FailsWhenOnlyTheSuffixIsCut) {
  const char* ids[] = {"KSFO"};
  char path[44];  // prefix + id + suffix is 44 characters, so it needs 45 with the terminator
  EXPECT_FALSE(metar_parse::buildRequestPath(path, sizeof(path), ids, 1));
}

TEST(MetarRequestPath, ZeroSizeBufferIsNeverWritten) {
  const char* ids[] = {"KSFO"};
  char guard[4] = {'x', 'x', 'x', 'x'};
  EXPECT_FALSE(metar_parse::buildRequestPath(guard, 0, ids, 1));
  EXPECT_EQ(guard[0], 'x');
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
