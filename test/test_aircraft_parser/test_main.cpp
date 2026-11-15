#include <gtest/gtest.h>

#include <ArduinoJson.h>

#include <cstring>
#include <string>

#include "../support/fixture.h"
#include "adsb_parse/aircraft_parser.h"

using adsb_parse::Aircraft;
using adsb_parse::kMaxAircraft;
using adsb_parse::ParseResult;
using adsb_parse::parseAircraft;

namespace {

ParseResult parse(const std::string& json, Aircraft* out, size_t capacity = kMaxAircraft,
                  bool include_ground = false) {
  return parseAircraft(json.data(), json.size(), include_ground, out, capacity);
}

/** Parse a single-aircraft document {"ac":[<plane>]} with ground aircraft included. */
Aircraft parseOne(const std::string& plane) {
  Aircraft out[1] = {};
  const ParseResult r = parse("{\"ac\":[" + plane + "]}", out, 1, true);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.count, 1u);
  return out[0];
}

}  // namespace

TEST(AircraftParserTest, AmsterdamFixtureParses) {
  const std::string json = readFixture("amsterdam_30nm.json");
  ASSERT_FALSE(json.empty());

  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, json));
  size_t expected = 0;
  JsonObject first_airborne;
  for (JsonObject plane : doc["ac"].as<JsonArray>()) {
    const bool positioned = plane["lat"].is<float>() && plane["lon"].is<float>();
    const bool ground = plane["alt_baro"] == "ground";
    if (positioned && !ground) {
      if (expected == 0) {
        first_airborne = plane;
      }
      ++expected;
    }
  }
  ASSERT_GT(expected, 0u);

  Aircraft out[kMaxAircraft] = {};
  const ParseResult r = parse(json, out);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.error, nullptr);
  EXPECT_EQ(r.count, expected);
  EXPECT_FLOAT_EQ(out[0].lat, first_airborne["lat"].as<float>());
  EXPECT_FLOAT_EQ(out[0].lon, first_airborne["lon"].as<float>());
}

TEST(AircraftParserTest, GroundAircraftSkippedByDefault) {
  Aircraft out[1] = {};
  const ParseResult r = parse(R"({"ac":[{"lat":1,"lon":2,"alt_baro":"ground"}]})", out, 1, false);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.count, 0u);
}

TEST(AircraftParserTest, GroundAircraftKeptWhenIncluded) {
  const Aircraft a = parseOne(R"({"lat":1,"lon":2,"alt_baro":"ground"})");
  EXPECT_STREQ(a.alt, "GND");
}

TEST(AircraftParserTest, MissingLatOrLonSkipped) {
  Aircraft out[2] = {};
  const ParseResult r = parse(R"({"ac":[{"lon":2},{"lat":1},{"lat":"x","lon":2}]})", out, 2);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.count, 0u);
}

TEST(AircraftParserTest, NoseHeadingFallbackOrder) {
  EXPECT_FLOAT_EQ(
      parseOne(R"({"lat":1,"lon":2,"true_heading":10,"mag_heading":20,"track":30,"dir":40})")
          .nose_deg,
      10.0f);
  EXPECT_FLOAT_EQ(
      parseOne(R"({"lat":1,"lon":2,"mag_heading":20,"track":30,"dir":40})").nose_deg, 20.0f);
  EXPECT_FLOAT_EQ(parseOne(R"({"lat":1,"lon":2,"track":30,"dir":40})").nose_deg, 30.0f);
  EXPECT_FLOAT_EQ(parseOne(R"({"lat":1,"lon":2,"dir":40.5})").nose_deg, 40.5f);
  EXPECT_FLOAT_EQ(parseOne(R"({"lat":1,"lon":2})").nose_deg, 0.0f);
}

TEST(AircraftParserTest, TrackHeadingFallbackOrder) {
  EXPECT_FLOAT_EQ(
      parseOne(R"({"lat":1,"lon":2,"true_heading":10,"mag_heading":20,"track":30,"dir":40})")
          .track_deg,
      30.0f);
  EXPECT_FLOAT_EQ(
      parseOne(R"({"lat":1,"lon":2,"true_heading":10,"mag_heading":20,"dir":40})").track_deg,
      10.0f);
  EXPECT_FLOAT_EQ(parseOne(R"({"lat":1,"lon":2,"mag_heading":20,"dir":40})").track_deg, 20.0f);
  EXPECT_FLOAT_EQ(parseOne(R"({"lat":1,"lon":2,"dir":40})").track_deg, 40.0f);
  EXPECT_FLOAT_EQ(parseOne(R"({"lat":1,"lon":2})").track_deg, 0.0f);
}

TEST(AircraftParserTest, GroundSpeedFallbackOrder) {
  EXPECT_FLOAT_EQ(parseOne(R"({"lat":1,"lon":2,"gs":100,"tas":200,"ias":300})").gs_knots,
                  100.0f);
  EXPECT_FLOAT_EQ(parseOne(R"({"lat":1,"lon":2,"tas":200,"ias":300})").gs_knots, 200.0f);
  EXPECT_FLOAT_EQ(parseOne(R"({"lat":1,"lon":2,"ias":300})").gs_knots, 300.0f);
  EXPECT_FLOAT_EQ(parseOne(R"({"lat":1,"lon":2})").gs_knots, 0.0f);
}

TEST(AircraftParserTest, CallsignTrimmedAndHexFallback) {
  EXPECT_STREQ(parseOne(R"({"lat":1,"lon":2,"flight":"KLM174  ","hex":"484f6d"})").callsign,
               "KLM174");
  EXPECT_STREQ(parseOne(R"({"lat":1,"lon":2,"hex":"484f6d"})").callsign, "484f6d");
  EXPECT_STREQ(parseOne(R"({"lat":1,"lon":2,"flight":"    ","hex":"484f6d"})").callsign,
               "484f6d");
  EXPECT_STREQ(parseOne(R"({"lat":1,"lon":2,"flight":"ABCDEFGHIJ"})").callsign, "ABCDEFGH");
  EXPECT_STREQ(parseOne(R"({"lat":1,"lon":2})").callsign, "");
}

TEST(AircraftParserTest, TypeTruncatedTo4) {
  EXPECT_STREQ(parseOne(R"({"lat":1,"lon":2,"t":"A20NX"})").type, "A20N");
  EXPECT_STREQ(parseOne(R"({"lat":1,"lon":2,"t":"B38M"})").type, "B38M");
}

TEST(AircraftParserTest, AltitudeTag) {
  EXPECT_STREQ(parseOne(R"({"lat":1,"lon":2,"alt_baro":35000})").alt, "35000 ft");
  EXPECT_STREQ(parseOne(R"({"lat":1,"lon":2,"alt_geom":1200.6})").alt, "1201 ft");
  EXPECT_STREQ(parseOne(R"({"lat":1,"lon":2,"alt_baro":"unknown","alt_geom":900})").alt,
               "900 ft");
  EXPECT_STREQ(parseOne(R"({"lat":1,"lon":2})").alt, "");
}

TEST(AircraftParserTest, EmptyAcArrayReturnsZero) {
  Aircraft out[1] = {};
  const ParseResult r = parse(R"({"ac":[],"now":1})", out);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.count, 0u);
}

TEST(AircraftParserTest, MissingAcFails) {
  Aircraft out[1] = {};
  const ParseResult r = parse(R"({"msg":"rate limited"})", out);
  EXPECT_FALSE(r.ok);
  EXPECT_STREQ(r.error, "no ac array");
  EXPECT_EQ(r.count, 0u);
}

TEST(AircraftParserTest, NonArrayAcFails) {
  Aircraft out[1] = {};
  ParseResult r = parse(R"({"ac":null})", out);
  EXPECT_FALSE(r.ok);
  EXPECT_STREQ(r.error, "no ac array");
  r = parse(R"({"ac":{"lat":1,"lon":2}})", out);
  EXPECT_FALSE(r.ok);
  r = parse("[]", out);
  EXPECT_FALSE(r.ok);
}

TEST(AircraftParserTest, CapsAtCapacity) {
  const std::string json = readFixture("many_aircraft.json");
  ASSERT_FALSE(json.empty());
  Aircraft out[kMaxAircraft] = {};
  const ParseResult r = parse(json, out, kMaxAircraft, true);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.count, kMaxAircraft);
}

TEST(AircraftParserTest, MalformedJsonFails) {
  Aircraft out[1] = {};
  const ParseResult r = parse("{\"ac\":[", out);
  EXPECT_FALSE(r.ok);
  EXPECT_NE(r.error, nullptr);
  EXPECT_EQ(r.count, 0u);
}

TEST(AircraftParserTest, FailureLeavesOutputUntouched) {
  Aircraft out[1] = {};
  strcpy(out[0].callsign, "KEEP");
  parse(R"({"ac":[{"lat":1,"lon":2,"flight":"NEW"},)", out);
  EXPECT_STREQ(out[0].callsign, "KEEP");
}

TEST(AircraftParserTest, FilterDropsUnusedKeys) {
  JsonDocument filter;
  adsb_parse::buildAircraftFilter(filter);
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, R"({"ac":[{"lat":1,"lon":2,"rssi":-3}],"now":1})",
                               DeserializationOption::Filter(filter)));
  EXPECT_EQ(doc["ac"][0]["lat"].as<int>(), 1);
  EXPECT_TRUE(doc["ac"][0]["rssi"].isNull());
  EXPECT_TRUE(doc["now"].isNull());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
