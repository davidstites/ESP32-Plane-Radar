#include <gtest/gtest.h>

#include <ArduinoJson.h>

#include <cstring>
#include <string>

#include "../support/fake_source.h"
#include "../support/fixture.h"
#include "adsb_parse/aircraft_parser.h"
#include "adsb_parse/body_reader.h"

using adsb_parse::Aircraft;
using adsb_parse::BodyFraming;
using adsb_parse::BodyReader;
using adsb_parse::BodyStatus;
using adsb_parse::kMaxAircraft;
using adsb_parse::ParseResult;

namespace {

constexpr uint32_t kTimeoutMs = 10000;

BodyReader makeReader(FakeSource& src, FakeClock& clock,
                      BodyFraming framing = BodyFraming::kChunked, long content_length = -1,
                      uint32_t timeout_ms = kTimeoutMs) {
  return BodyReader(src.fn(), framing, content_length, clock.nowFn(), clock.idleFn(),
                    timeout_ms);
}

/** Read the whole body through readBytes() in 7-byte requests. */
std::string readAll(BodyReader& reader) {
  std::string out;
  char buf[7];
  size_t n;
  while ((n = reader.readBytes(buf, sizeof(buf))) > 0) {
    out.append(buf, n);
  }
  return out;
}

std::string decode(const std::string& raw, size_t max_per_read, BodyStatus* status) {
  FakeSource src(raw);
  src.max_per_read = max_per_read;
  FakeClock clock;
  BodyReader reader = makeReader(src, clock);
  const std::string out = readAll(reader);
  *status = reader.status();
  return out;
}

}  // namespace

TEST(BodyReaderTest, SingleChunk) {
  FakeSource src("5\r\nhello\r\n0\r\n\r\n");
  FakeClock clock;
  BodyReader reader = makeReader(src, clock);
  EXPECT_EQ(readAll(reader), "hello");
  EXPECT_EQ(reader.status(), BodyStatus::kComplete);
  EXPECT_TRUE(reader.reusable());
}

TEST(BodyReaderTest, ManyChunks) {
  FakeSource src("3\r\nabc\r\n2\r\nde\r\n4\r\nfghi\r\n0\r\n\r\n");
  FakeClock clock;
  BodyReader reader = makeReader(src, clock);
  EXPECT_EQ(readAll(reader), "abcdefghi");
  EXPECT_EQ(reader.status(), BodyStatus::kComplete);
}

TEST(BodyReaderTest, ByteAtATimeMatchesBulk) {
  const std::string raw = readFixture("amsterdam_30nm.chunked");
  ASSERT_FALSE(raw.empty());
  BodyStatus slow_status;
  BodyStatus bulk_status;
  const std::string slow = decode(raw, 1, &slow_status);
  const std::string bulk = decode(raw, 256, &bulk_status);
  EXPECT_EQ(slow, bulk);
  EXPECT_EQ(slow_status, BodyStatus::kComplete);
  EXPECT_EQ(bulk_status, BodyStatus::kComplete);
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, slow));
  EXPECT_TRUE(doc["ac"].is<JsonArray>());
}

TEST(BodyReaderTest, SizeLineSplitAcrossReads) {
  FakeSource src("10\r\n0123456789abcdef\r\n0\r\n\r\n");
  src.read_sizes = {1, 2, 1};
  FakeClock clock;
  BodyReader reader = makeReader(src, clock);
  EXPECT_EQ(readAll(reader), "0123456789abcdef");
  EXPECT_EQ(reader.status(), BodyStatus::kComplete);
}

TEST(BodyReaderTest, ChunkExtensionIgnored) {
  FakeSource src("5;foo=bar\r\nhello\r\n0;last\r\n\r\n");
  FakeClock clock;
  BodyReader reader = makeReader(src, clock);
  EXPECT_EQ(readAll(reader), "hello");
  EXPECT_EQ(reader.status(), BodyStatus::kComplete);
}

TEST(BodyReaderTest, UppercaseAndLowercaseHex) {
  const std::string ten = "0123456789";
  FakeSource src("A\r\n" + ten + "\r\na\r\n" + ten + "\r\n0\r\n\r\n");
  FakeClock clock;
  BodyReader reader = makeReader(src, clock);
  EXPECT_EQ(readAll(reader), ten + ten);
  EXPECT_EQ(reader.status(), BodyStatus::kComplete);
}

TEST(BodyReaderTest, TrailersConsumed) {
  FakeSource src("2\r\nhi\r\n0\r\nX-T: 1\r\nX-U: 2\r\n\r\n");
  FakeClock clock;
  BodyReader reader = makeReader(src, clock);
  EXPECT_EQ(readAll(reader), "hi");
  EXPECT_EQ(reader.status(), BodyStatus::kComplete);
  EXPECT_EQ(src.pos, src.data.size());
}

TEST(BodyReaderTest, BareLfLineEndingsAccepted) {
  FakeSource src("2\nhi\n0\nX-T: 1\n\n");
  FakeClock clock;
  BodyReader reader = makeReader(src, clock);
  EXPECT_EQ(readAll(reader), "hi");
  EXPECT_EQ(reader.status(), BodyStatus::kComplete);
}

TEST(BodyReaderTest, NonHexSizeIsMalformed) {
  FakeSource src("zz\r\nhello\r\n0\r\n\r\n");
  FakeClock clock;
  BodyReader reader = makeReader(src, clock);
  EXPECT_EQ(reader.read(), -1);
  EXPECT_EQ(reader.status(), BodyStatus::kMalformed);
  EXPECT_FALSE(reader.reusable());
}

TEST(BodyReaderTest, EmptySizeLineIsMalformed) {
  FakeSource src("\r\nhello\r\n0\r\n\r\n");
  FakeClock clock;
  BodyReader reader = makeReader(src, clock);
  EXPECT_EQ(reader.read(), -1);
  EXPECT_EQ(reader.status(), BodyStatus::kMalformed);
}

TEST(BodyReaderTest, OversizedChunkSizeIsMalformed) {
  FakeSource src("123456789\r\n");
  FakeClock clock;
  BodyReader reader = makeReader(src, clock);
  EXPECT_EQ(reader.read(), -1);
  EXPECT_EQ(reader.status(), BodyStatus::kMalformed);
}

TEST(BodyReaderTest, MissingCrlfAfterDataIsMalformed) {
  FakeSource src("5\r\nhelloX\r\n0\r\n\r\n");
  FakeClock clock;
  BodyReader reader = makeReader(src, clock);
  EXPECT_EQ(readAll(reader), "hello");
  EXPECT_EQ(reader.status(), BodyStatus::kMalformed);

  FakeSource cr_only("5\r\nhello\rX0\r\n\r\n");
  BodyReader reader2 = makeReader(cr_only, clock);
  EXPECT_EQ(readAll(reader2), "hello");
  EXPECT_EQ(reader2.status(), BodyStatus::kMalformed);
}

TEST(BodyReaderTest, TruncatedChunkedBodyIsClosedAndNotReusable) {
  FakeSource src("a\r\nhello");
  FakeClock clock;
  BodyReader reader = makeReader(src, clock);
  EXPECT_EQ(readAll(reader), "hello");
  EXPECT_EQ(reader.status(), BodyStatus::kClosed);
  EXPECT_FALSE(reader.reusable());
  EXPECT_FALSE(reader.drain());
}

TEST(BodyReaderTest, TimeoutWhenSourceStalls) {
  FakeSource src("5\r\nhe");
  src.close_at_end = false;
  FakeClock clock;
  clock.idle_step = 100;
  BodyReader reader = makeReader(src, clock, BodyFraming::kChunked, -1, 1000);
  EXPECT_EQ(readAll(reader), "he");
  EXPECT_EQ(reader.status(), BodyStatus::kTimeout);
  EXPECT_FALSE(reader.reusable());
  EXPECT_GE(clock.idle_calls, 10u);
}

TEST(BodyReaderTest, TimeoutMeasuredFromConstruction) {
  FakeSource src("5\r\nhello\r\n0\r\n\r\n");
  FakeClock clock;
  clock.now = 4000000000u;  // close to uint32_t wrap
  BodyReader reader = makeReader(src, clock, BodyFraming::kChunked, -1, 1000);
  clock.now += 1000;  // wraps; still exactly at the deadline
  EXPECT_EQ(reader.read(), -1);
  EXPECT_EQ(reader.status(), BodyStatus::kTimeout);
}

TEST(BodyReaderTest, IdleCalledWhileWaiting) {
  FakeSource src("5\r\nhello\r\n0\r\n\r\n");
  src.read_sizes = {0, 0};
  FakeClock clock;
  BodyReader reader = makeReader(src, clock);
  EXPECT_EQ(readAll(reader), "hello");
  EXPECT_EQ(clock.idle_calls, 2u);
}

TEST(BodyReaderTest, ContentLengthStopsAtLength) {
  FakeSource src("helloEXTRA");
  FakeClock clock;
  BodyReader reader = makeReader(src, clock, BodyFraming::kContentLength, 5);
  EXPECT_EQ(readAll(reader), "hello");
  EXPECT_EQ(reader.status(), BodyStatus::kComplete);
  EXPECT_TRUE(reader.reusable());
  EXPECT_EQ(src.pos, 5u);
}

TEST(BodyReaderTest, ContentLengthZeroIsCompleteImmediately) {
  FakeSource src("");
  FakeClock clock;
  BodyReader reader = makeReader(src, clock, BodyFraming::kContentLength, 0);
  EXPECT_EQ(reader.read(), -1);
  EXPECT_EQ(reader.status(), BodyStatus::kComplete);
  EXPECT_EQ(src.calls, 0u);
}

TEST(BodyReaderTest, ContentLengthShortIsClosed) {
  FakeSource src("hello");
  FakeClock clock;
  BodyReader reader = makeReader(src, clock, BodyFraming::kContentLength, 10);
  EXPECT_EQ(readAll(reader), "hello");
  EXPECT_EQ(reader.status(), BodyStatus::kClosed);
  EXPECT_FALSE(reader.reusable());
}

TEST(BodyReaderTest, UntilCloseCompletesOnClose) {
  FakeSource src("hello world");
  FakeClock clock;
  BodyReader reader = makeReader(src, clock, BodyFraming::kUntilClose);
  EXPECT_EQ(readAll(reader), "hello world");
  EXPECT_EQ(reader.status(), BodyStatus::kComplete);
  EXPECT_FALSE(reader.reusable());
}

TEST(BodyReaderTest, NeverRequestsMoreThanBufferSize) {
  const std::string body(1000, 'x');
  FakeSource src(chunked(body, 600));
  FakeClock clock;
  BodyReader reader = makeReader(src, clock);
  EXPECT_EQ(readAll(reader), body);
  EXPECT_LE(src.largest_request, 256u);
  EXPECT_EQ(BodyReader::kBufferSize, 256u);
}

TEST(BodyReaderTest, DrainAfterPartialReadCompletes) {
  FakeSource src("3\r\nabc\r\n3\r\ndef\r\n3\r\nghi\r\n0\r\n\r\n");
  FakeClock clock;
  BodyReader reader = makeReader(src, clock);
  char buf[2];
  EXPECT_EQ(reader.readBytes(buf, 2), 2u);
  EXPECT_EQ(reader.status(), BodyStatus::kReading);
  EXPECT_TRUE(reader.drain());
  EXPECT_EQ(reader.status(), BodyStatus::kComplete);
  EXPECT_TRUE(reader.reusable());
  EXPECT_EQ(reader.read(), -1);
}

TEST(BodyReaderTest, DrainsWholeErrorBodyWithoutReading) {
  const std::string body = "{\"error\":\"rate limited\"}";
  FakeSource chunked_src(chunked(body, 10));
  FakeClock clock;
  BodyReader chunked_reader = makeReader(chunked_src, clock);
  EXPECT_TRUE(chunked_reader.drain());
  EXPECT_TRUE(chunked_reader.reusable());
  EXPECT_EQ(chunked_src.pos, chunked_src.data.size());

  FakeSource length_src(body + "NEXT");
  BodyReader length_reader =
      makeReader(length_src, clock, BodyFraming::kContentLength, static_cast<long>(body.size()));
  EXPECT_TRUE(length_reader.drain());
  EXPECT_TRUE(length_reader.reusable());
  EXPECT_EQ(length_src.pos, body.size());
}

namespace {

ParseResult parseStream(FakeSource& src, Aircraft* out, BodyStatus* status,
                        bool include_ground = false) {
  FakeClock clock;
  BodyReader reader = makeReader(src, clock);
  const ParseResult r = adsb_parse::parseAircraft(reader, include_ground, out, kMaxAircraft);
  *status = reader.status();
  return r;
}

}  // namespace

TEST(ParseStreamTest, ChunkedFixtureMatchesInMemoryParse) {
  const std::string json = readFixture("amsterdam_30nm.json");
  FakeSource src(readFixture("amsterdam_30nm.chunked"));
  ASSERT_FALSE(json.empty());

  Aircraft expected[kMaxAircraft] = {};
  const ParseResult want =
      adsb_parse::parseAircraft(json.data(), json.size(), true, expected, kMaxAircraft);
  Aircraft actual[kMaxAircraft] = {};
  BodyStatus status;
  const ParseResult got = parseStream(src, actual, &status, true);

  ASSERT_TRUE(want.ok);
  ASSERT_TRUE(got.ok);
  EXPECT_EQ(got.error, nullptr);
  ASSERT_GT(want.count, 0u);
  ASSERT_EQ(got.count, want.count);
  EXPECT_EQ(memcmp(actual, expected, sizeof(Aircraft) * want.count), 0);
  EXPECT_EQ(status, BodyStatus::kComplete);
}

TEST(ParseStreamTest, ManyAircraftFixtureByteAtATime) {
  FakeSource src(chunked(readFixture("many_aircraft.json"), 100));
  src.max_per_read = 1;
  Aircraft out[kMaxAircraft] = {};
  BodyStatus status;
  const ParseResult r = parseStream(src, out, &status, true);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.count, kMaxAircraft);
  EXPECT_EQ(status, BodyStatus::kComplete);
}

TEST(ParseStreamTest, MalformedJsonStillDrainsAndReuses) {
  FakeSource src(chunked("{\"ac\":[{garbage that is not json at all}]}", 8));
  Aircraft out[kMaxAircraft] = {};
  FakeClock clock;
  BodyReader reader = makeReader(src, clock);
  const ParseResult r = adsb_parse::parseAircraft(reader, false, out, kMaxAircraft);
  EXPECT_FALSE(r.ok);
  EXPECT_NE(r.error, nullptr);
  EXPECT_EQ(reader.status(), BodyStatus::kComplete);
  EXPECT_TRUE(reader.reusable());
  EXPECT_EQ(src.pos, src.data.size());
}

TEST(ParseStreamTest, TruncatedBodyFailsAndLeavesOutputUntouched) {
  const std::string raw = readFixture("amsterdam_30nm.chunked");
  ASSERT_GT(raw.size(), 1000u);
  FakeSource src(raw.substr(0, raw.size() / 2));
  Aircraft out[kMaxAircraft] = {};
  strcpy(out[0].callsign, "KEEP");
  FakeClock clock;
  BodyReader reader = makeReader(src, clock);
  const ParseResult r = adsb_parse::parseAircraft(reader, true, out, kMaxAircraft);
  EXPECT_FALSE(r.ok);
  EXPECT_STREQ(out[0].callsign, "KEEP");
  EXPECT_EQ(reader.status(), BodyStatus::kClosed);
  EXPECT_FALSE(reader.reusable());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
