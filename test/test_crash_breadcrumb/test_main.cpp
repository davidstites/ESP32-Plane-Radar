#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "crash_breadcrumb/crash_breadcrumb.h"

using namespace crash_breadcrumb;

namespace {

Report knownReport() {
  Report r{};
  r.resetReason = "panic";
  r.checkpoint = "net:ota";
  r.checkpointUptimeMs = 1234;
  r.runningBuild = 8;
  r.pendingBuild = 9;
  r.blacklist[0] = 3;
  r.blacklist[1] = 4;
  r.blacklistCount = 2;
  r.heapFree = 50000;
  r.heapLowest = 30000;
  r.slot = "app1";
  return r;
}

}  // namespace

TEST(Breadcrumb, SetTruncatesAndTerminates) {
  Breadcrumb b{};
  const std::string longLabel(100, 'x');
  set(b, longLabel.c_str(), 77);
  EXPECT_EQ(b.magic, kMagic);
  EXPECT_EQ(b.uptimeMs, 77u);
  EXPECT_EQ(strlen(b.label), kLabelMax - 1);
  EXPECT_EQ(b.label[kLabelMax - 1], '\0');
}

TEST(Breadcrumb, SetOverwritesAnEarlierLongerLabel) {
  Breadcrumb b{};
  set(b, "a-rather-long-label", 1);
  set(b, "ok", 2);
  EXPECT_STREQ(b.label, "ok");
  EXPECT_EQ(b.label[10], '\0');
}

TEST(Breadcrumb, CopyLabelRoundTrip) {
  Breadcrumb b{};
  set(b, "net:ota", 5);
  char out[32];
  EXPECT_EQ(copyLabel(b, out, sizeof(out)), 7u);
  EXPECT_STREQ(out, "net:ota");
}

TEST(Breadcrumb, GarbageMagicReadsEmpty) {
  Breadcrumb b{};
  set(b, "net:ota", 5);
  b.magic ^= 1;
  char out[32] = "xx";
  EXPECT_EQ(copyLabel(b, out, sizeof(out)), 0u);
  EXPECT_STREQ(out, "");
}

TEST(Breadcrumb, NonPrintableReadsEmpty) {
  Breadcrumb b{};
  set(b, "net:ota", 5);
  b.label[3] = '\x01';
  char out[32] = "xx";
  EXPECT_EQ(copyLabel(b, out, sizeof(out)), 0u);
  EXPECT_STREQ(out, "");
  b.label[3] = static_cast<char>(0xFF);
  EXPECT_EQ(copyLabel(b, out, sizeof(out)), 0u);
  b.label[3] = '\x7f';
  EXPECT_EQ(copyLabel(b, out, sizeof(out)), 0u);
}

TEST(Breadcrumb, UnterminatedLabelReadsEmpty) {
  Breadcrumb b{};
  b.magic = kMagic;
  memset(b.label, 'a', sizeof(b.label));  // no NUL anywhere
  char out[32] = "xx";
  EXPECT_EQ(copyLabel(b, out, sizeof(out)), 0u);
  EXPECT_STREQ(out, "");
}

TEST(Breadcrumb, CopyLabelRespectsSmallCap) {
  Breadcrumb b{};
  set(b, "net:ota", 5);
  char out[4];
  EXPECT_EQ(copyLabel(b, out, sizeof(out)), 3u);
  EXPECT_STREQ(out, "net");
  char one[1] = {'x'};
  EXPECT_EQ(copyLabel(b, one, 1), 0u);
  EXPECT_EQ(one[0], '\0');
  EXPECT_EQ(copyLabel(b, nullptr, 0), 0u);
}

TEST(ReportJson, ExactForKnownInput) {
  char out[512];
  const size_t n = formatReportJson(knownReport(), out, sizeof(out));
  const std::string want =
      "{\"reset\":\"panic\",\"checkpoint\":\"net:ota\",\"checkpoint_ms\":1234,\"build\":8,\"pending\":9,"
      "\"blacklist\":[3,4],\"heap_free\":50000,\"heap_min\":30000,\"slot\":\"app1\"}";
  EXPECT_EQ(std::string(out), want);
  EXPECT_EQ(n, want.size());
}

TEST(ReportJson, EscapesQuoteBackslashAndControlCharacters) {
  Report r = knownReport();
  r.resetReason = "a\"b\\c";
  r.checkpoint = "x\ny";
  char out[512];
  ASSERT_GT(formatReportJson(r, out, sizeof(out)), 0u);
  EXPECT_NE(std::string(out).find("\"reset\":\"a\\\"b\\\\c\""), std::string::npos);
  EXPECT_NE(std::string(out).find("\"checkpoint\":\"x\\u000ay\""), std::string::npos);
}

TEST(ReportJson, NullStringsBecomeEmpty) {
  Report r = knownReport();
  r.resetReason = nullptr;
  r.checkpoint = nullptr;
  r.slot = nullptr;
  char out[512];
  ASSERT_GT(formatReportJson(r, out, sizeof(out)), 0u);
  EXPECT_NE(std::string(out).find("\"reset\":\"\""), std::string::npos);
  EXPECT_NE(std::string(out).find("\"slot\":\"\""), std::string::npos);
}

TEST(ReportJson, EmptyBlacklistIsEmptyArray) {
  Report r = knownReport();
  r.blacklistCount = 0;
  char out[512];
  ASSERT_GT(formatReportJson(r, out, sizeof(out)), 0u);
  EXPECT_NE(std::string(out).find("\"blacklist\":[]"), std::string::npos);
}

TEST(ReportJson, BlacklistCountIsClampedToTheArray) {
  Report r = knownReport();
  r.blacklistCount = 100;
  for (size_t i = 0; i < kMaxBlacklist; ++i) r.blacklist[i] = static_cast<uint32_t>(i + 1);
  char out[512];
  ASSERT_GT(formatReportJson(r, out, sizeof(out)), 0u);
  EXPECT_NE(std::string(out).find("\"blacklist\":[1,2,3,4,5,6,7,8]"), std::string::npos);
}

TEST(ReportJson, TooSmallReturnsZeroAtEveryTruncationPoint) {
  char full[512];
  const size_t n = formatReportJson(knownReport(), full, sizeof(full));
  ASSERT_GT(n, 0u);
  char exact[512];
  EXPECT_EQ(formatReportJson(knownReport(), exact, n + 1), n);  // length plus NUL fits
  for (size_t cap = 0; cap <= n; ++cap) {
    char small[512];
    EXPECT_EQ(formatReportJson(knownReport(), small, cap), 0u) << cap;
  }
  EXPECT_EQ(formatReportJson(knownReport(), nullptr, 0), 0u);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
