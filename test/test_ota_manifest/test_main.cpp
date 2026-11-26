#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "ota_core/manifest.h"

using namespace ota_core;

namespace {

const std::string kSha(64, 'a');
const std::string kSig(88, 'B');
const std::string kHash1 = "0123456789abcdef0123456789abcdef";
const std::string kHash2 = "fedcba9876543210fedcba9876543210";

// Each entry is a complete `"key":value` pair, so a test can drop or replace one.
std::vector<std::pair<std::string, std::string>> fullPairs() {
  return {
      {"build", "8"},
      {"min_build", "5"},
      {"sha256", "\"" + kSha + "\""},
      {"channel", "\"prod\""},
      {"signature", "\"" + kSig + "\""},
      {"firmware", "\"/planeradar/firmware-8.bin\""},
  };
}

std::string toJson(const std::vector<std::pair<std::string, std::string>>& pairs) {
  std::string s = "{";
  for (size_t i = 0; i < pairs.size(); ++i) {
    if (i) s += ",";
    s += "\"" + pairs[i].first + "\":" + pairs[i].second;
  }
  return s + "}";
}

ParseError parse(const std::string& body, Manifest& out) {
  return parseManifest(body.c_str(), body.size(), out);
}

ParseError parsePairs(const std::vector<std::pair<std::string, std::string>>& pairs) {
  Manifest m{};
  return parse(toJson(pairs), m);
}

std::vector<std::pair<std::string, std::string>> withExtra(const std::string& key, const std::string& value) {
  auto p = fullPairs();
  p.emplace_back(key, value);
  return p;
}

std::vector<std::pair<std::string, std::string>> replaced(const std::string& key, const std::string& value) {
  auto p = fullPairs();
  for (auto& kv : p) {
    if (kv.first == key) kv.second = value;
  }
  return p;
}

}  // namespace

TEST(Manifest, ParsesFullManifest) {
  auto pairs = withExtra("rollout", "25");
  pairs.emplace_back("candidates", "[\"" + kHash1 + "\",\"" + kHash2 + "\"]");
  Manifest m{};
  ASSERT_EQ(parse(toJson(pairs), m), ParseError::kNone);
  EXPECT_TRUE(m.build.valid);
  EXPECT_EQ(m.build.value, 8u);
  EXPECT_TRUE(m.minBuild.valid);
  EXPECT_EQ(m.minBuild.value, 5u);
  EXPECT_STREQ(m.sha256, kSha.c_str());
  EXPECT_STREQ(m.channel, "prod");
  EXPECT_STREQ(m.signature, kSig.c_str());
  EXPECT_STREQ(m.firmware, "/planeradar/firmware-8.bin");
  EXPECT_EQ(m.rollout, 25);
  ASSERT_EQ(m.candidateCount, 2u);
  EXPECT_STREQ(m.candidates[0], kHash1.c_str());
  EXPECT_STREQ(m.candidates[1], kHash2.c_str());
}

TEST(Manifest, FieldsPointAtTheParsedStrings) {
  Manifest m{};
  ASSERT_EQ(parse(toJson(fullPairs()), m), ParseError::kNone);
  const ManifestFields f = m.fields();
  EXPECT_TRUE(f.build.valid);
  EXPECT_EQ(f.build.value, 8u);
  EXPECT_EQ(f.minBuild.value, 5u);
  EXPECT_EQ(f.sha256Hex, m.sha256);
  EXPECT_EQ(f.channel, m.channel);
  EXPECT_EQ(f.signature, m.signature);
}

TEST(Manifest, RolloutDefaultsTo100AndNoCandidates) {
  Manifest m{};
  ASSERT_EQ(parse(toJson(fullPairs()), m), ParseError::kNone);
  EXPECT_EQ(m.rollout, 100);
  EXPECT_EQ(m.candidateCount, 0u);
}

TEST(Manifest, RolloutBoundaries) {
  Manifest m{};
  ASSERT_EQ(parse(toJson(withExtra("rollout", "0")), m), ParseError::kNone);
  EXPECT_EQ(m.rollout, 0);
  ASSERT_EQ(parse(toJson(withExtra("rollout", "100")), m), ParseError::kNone);
  EXPECT_EQ(m.rollout, 100);
}

TEST(Manifest, RolloutOver100IsBadField) {
  EXPECT_EQ(parsePairs(withExtra("rollout", "101")), ParseError::kBadField);
  EXPECT_EQ(parsePairs(withExtra("rollout", "-1")), ParseError::kBadField);
  EXPECT_EQ(parsePairs(withExtra("rollout", "\"50\"")), ParseError::kBadField);
  EXPECT_EQ(parsePairs(withExtra("rollout", "50.5")), ParseError::kBadField);
  EXPECT_EQ(parsePairs(withExtra("rollout", "null")), ParseError::kBadField);
}

TEST(Manifest, NonObjectIsNotJson) {
  Manifest m{};
  EXPECT_EQ(parse("[1,2,3]", m), ParseError::kNotJson);
  EXPECT_EQ(parse("42", m), ParseError::kNotJson);
  EXPECT_EQ(parse("not json at all", m), ParseError::kNotJson);
  EXPECT_EQ(parse("", m), ParseError::kNotJson);
  EXPECT_EQ(parse("{\"build\":", m), ParseError::kNotJson);
}

TEST(Manifest, OverCapIsTooLarge) {
  Manifest m{};
  std::string big = toJson(fullPairs());
  big.insert(1, std::string(1025 - big.size(), ' '));
  ASSERT_EQ(big.size(), 1025u);
  EXPECT_EQ(parse(big, m), ParseError::kTooLarge);
  std::string ok = toJson(fullPairs());
  ok.insert(1, std::string(1024 - ok.size(), ' '));
  ASSERT_EQ(ok.size(), 1024u);
  EXPECT_EQ(parse(ok, m), ParseError::kNone);
}

class MissingField : public ::testing::TestWithParam<const char*> {};

TEST_P(MissingField, IsReported) {
  auto pairs = fullPairs();
  for (size_t i = 0; i < pairs.size(); ++i) {
    if (pairs[i].first == GetParam()) {
      pairs.erase(pairs.begin() + static_cast<long>(i));
      break;
    }
  }
  EXPECT_EQ(parsePairs(pairs), ParseError::kMissingField);
}

INSTANTIATE_TEST_SUITE_P(Required, MissingField,
                         ::testing::Values("build", "min_build", "sha256", "channel", "signature", "firmware"));

TEST(Manifest, BuildAsStringIsInvalidNotMissing) {
  Manifest m{};
  ASSERT_EQ(parse(toJson(replaced("build", "\"8\"")), m), ParseError::kNone);
  EXPECT_FALSE(m.build.valid);
  EXPECT_TRUE(m.minBuild.valid);
  ASSERT_EQ(parse(toJson(replaced("min_build", "\"5\"")), m), ParseError::kNone);
  EXPECT_FALSE(m.minBuild.valid);
}

TEST(Manifest, HugeNegativeAndFractionalBuildsAreInvalid) {
  Manifest m{};
  ASSERT_EQ(parse(toJson(replaced("build", "4294967296")), m), ParseError::kNone);
  EXPECT_FALSE(m.build.valid);
  ASSERT_EQ(parse(toJson(replaced("build", "-3")), m), ParseError::kNone);
  EXPECT_FALSE(m.build.valid);
  ASSERT_EQ(parse(toJson(replaced("build", "8.5")), m), ParseError::kNone);
  EXPECT_FALSE(m.build.valid);
  ASSERT_EQ(parse(toJson(replaced("build", "4294967295")), m), ParseError::kNone);
  EXPECT_TRUE(m.build.valid);
  EXPECT_EQ(m.build.value, 4294967295u);
}

TEST(Manifest, WrongTypedStringFieldsAreBadField) {
  EXPECT_EQ(parsePairs(replaced("sha256", "7")), ParseError::kBadField);
  EXPECT_EQ(parsePairs(replaced("channel", "[]")), ParseError::kBadField);
  EXPECT_EQ(parsePairs(replaced("signature", "true")), ParseError::kBadField);
  EXPECT_EQ(parsePairs(replaced("firmware", "{}")), ParseError::kBadField);
}

TEST(Manifest, TooManyCandidatesIsBadField) {
  std::string list = "[";
  for (int i = 0; i < 9; ++i) {
    if (i) list += ",";
    list += "\"" + kHash1 + "\"";
  }
  list += "]";
  EXPECT_EQ(parsePairs(withExtra("candidates", list)), ParseError::kBadField);
  std::string eight = "[";
  for (int i = 0; i < 8; ++i) {
    if (i) eight += ",";
    eight += "\"" + kHash1 + "\"";
  }
  eight += "]";
  Manifest m{};
  ASSERT_EQ(parse(toJson(withExtra("candidates", eight)), m), ParseError::kNone);
  EXPECT_EQ(m.candidateCount, 8u);
}

TEST(Manifest, MalformedCandidatesAreBadField) {
  std::string upper = kHash1;
  upper[3] = 'A';
  EXPECT_EQ(parsePairs(withExtra("candidates", "[\"" + upper + "\"]")), ParseError::kBadField);
  EXPECT_EQ(parsePairs(withExtra("candidates", "[\"" + kHash1.substr(0, 31) + "\"]")), ParseError::kBadField);
  EXPECT_EQ(parsePairs(withExtra("candidates", "[\"" + kHash1 + "0\"]")), ParseError::kBadField);
  std::string nonhex = kHash1;
  nonhex[0] = 'g';
  EXPECT_EQ(parsePairs(withExtra("candidates", "[\"" + nonhex + "\"]")), ParseError::kBadField);
  EXPECT_EQ(parsePairs(withExtra("candidates", "[7]")), ParseError::kBadField);
  EXPECT_EQ(parsePairs(withExtra("candidates", "\"" + kHash1 + "\"")), ParseError::kBadField);
  EXPECT_EQ(parsePairs(withExtra("candidates", "{}")), ParseError::kBadField);
}

TEST(Manifest, EmptyCandidatesIsFine) {
  Manifest m{};
  ASSERT_EQ(parse(toJson(withExtra("candidates", "[]")), m), ParseError::kNone);
  EXPECT_EQ(m.candidateCount, 0u);
}

TEST(Manifest, LongStringsAreBadFieldNeverTruncated) {
  EXPECT_EQ(parsePairs(replaced("signature", "\"" + std::string(89, 'B') + "\"")), ParseError::kBadField);
  EXPECT_EQ(parsePairs(replaced("firmware", "\"/" + std::string(96, 'a') + "\"")), ParseError::kBadField);
  EXPECT_EQ(parsePairs(replaced("channel", "\"abcdefgh\"")), ParseError::kBadField);
  EXPECT_EQ(parsePairs(replaced("sha256", "\"" + std::string(65, 'a') + "\"")), ParseError::kBadField);
}

TEST(Manifest, MaxLengthStringsFit) {
  Manifest m{};
  const std::string path = "/" + std::string(95, 'a');
  auto pairs = replaced("firmware", "\"" + path + "\"");
  pairs = [&] {
    for (auto& kv : pairs) {
      if (kv.first == "channel") kv.second = "\"abcdefg\"";
    }
    return pairs;
  }();
  ASSERT_EQ(parse(toJson(pairs), m), ParseError::kNone);
  EXPECT_STREQ(m.firmware, path.c_str());
  EXPECT_STREQ(m.channel, "abcdefg");
}

TEST(Manifest, UnknownKeysIgnored) {
  Manifest m{};
  ASSERT_EQ(parse(toJson(withExtra("future", "{\"x\":[1,2]}")), m), ParseError::kNone);
  EXPECT_EQ(m.build.value, 8u);
}

TEST(Manifest, DuplicateKeysLastWins) {
  Manifest m{};
  auto pairs = fullPairs();
  pairs.emplace_back("build", "9");
  ASSERT_EQ(parse(toJson(pairs), m), ParseError::kNone);
  EXPECT_EQ(m.build.value, 9u);
}

TEST(Manifest, ResetsEarlierContentsOfOut) {
  Manifest m{};
  auto pairs = withExtra("rollout", "10");
  pairs.emplace_back("candidates", "[\"" + kHash1 + "\"]");
  ASSERT_EQ(parse(toJson(pairs), m), ParseError::kNone);
  ASSERT_EQ(parse(toJson(fullPairs()), m), ParseError::kNone);
  EXPECT_EQ(m.rollout, 100);
  EXPECT_EQ(m.candidateCount, 0u);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
