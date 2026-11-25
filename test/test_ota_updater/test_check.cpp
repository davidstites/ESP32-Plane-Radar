#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "ota_core/updater.h"
#include "support/ota_fakes.h"
#include "support/ota_vectors.h"
#include "support/tweetnacl_verify.h"

using namespace ota_core;
using namespace ota_fakes;
namespace v = ota_vectors;

namespace {

const char kManifestPath[] = "/planeradar/manifest.json";
const char kImage[] = "planeradar test firmware image";  // sha256 is v::kSha
const uint8_t* const kOneKey[] = {v::kPub};
const uint8_t* const kOtherKey[] = {v::kOtherPub};

struct Options {
  std::string sha = v::kShaHex;
  std::string signature = v::kSigProd;
  std::string channel = "prod";
  std::string firmware = "/planeradar/firmware-8.bin";
  std::string build = "8";
  std::string rollout;  // empty: omitted
};

std::string body(const Options& o = Options()) {
  std::string s = "{\"build\":" + o.build + ",\"min_build\":5,\"sha256\":\"" + o.sha + "\",\"channel\":\"" +
                  o.channel + "\",\"signature\":\"" + o.signature + "\",\"firmware\":\"" + o.firmware + "\"";
  if (!o.rollout.empty()) s += ",\"rollout\":" + o.rollout;
  return s + "}";
}

struct Rig {
  FakeStore store;
  FakeSlots slots;
  FakeHttp http;
  Log log;
  Config cfg{};
  Rig() {
    store.log = &log;
    slots.log = &log;
    cfg.basePath = "/planeradar";
    memcpy(cfg.deviceId, v::kDeviceId, sizeof(cfg.deviceId));
    cfg.runningBuild = 6;
    cfg.keys = KeyList{kOneKey, 1};
    cfg.verify = tweetnaclEd25519Verify;
    serve(body());
    http.image = kImage;
    http.openResult = static_cast<int64_t>(http.image.size());
  }
  void serve(const std::string& manifest, int status = 200, const std::string& etag = "\"v8\"") {
    FakeHttp::Manifest m;
    m.status = status;
    m.etag = etag;
    m.body = manifest;
    http.manifests[kManifestPath] = m;
  }
  CheckResult run() {
    Updater u(cfg, store, slots, http);
    return u.check();
  }
  std::string storedEtag() {
    char buf[80];
    OtaState(store).etag(buf, sizeof(buf));
    return buf;
  }
  size_t indexOf(const std::string& entry) const {
    return static_cast<size_t>(std::find(log.begin(), log.end(), entry) - log.begin());
  }
  bool logged(const std::string& entry) const { return indexOf(entry) < log.size(); }
};

void expectAbortedWithoutActivation(Rig& r) {
  EXPECT_EQ(r.slots.endCalls, 0);
  EXPECT_TRUE(r.logged("abortUpdate"));
  EXPECT_EQ(r.store.uints.count("pending"), 0u);
  EXPECT_EQ(r.storedEtag(), "");
  EXPECT_TRUE(r.http.closed);
}

}  // namespace

TEST(Check, ControlInstalls) {
  Rig r;
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kInstalled);
  EXPECT_STREQ(res.reason, "installed");
  EXPECT_EQ(res.build, 8u);
  EXPECT_FALSE(res.candidate);
  EXPECT_EQ(r.slots.endCalls, 1);
  EXPECT_EQ(std::string(r.slots.written.begin(), r.slots.written.end()), kImage);
  EXPECT_EQ(r.store.uints["pending"], 8u);
  EXPECT_LT(r.indexOf("beginUpdate"), r.indexOf("putUInt:pending"));
  EXPECT_LT(r.indexOf("putUInt:pending"), r.indexOf("endUpdate"));
  EXPECT_EQ(r.http.openedPaths, std::vector<std::string>{"/planeradar/firmware-8.bin"});
  EXPECT_TRUE(r.http.closed);
  EXPECT_EQ(r.storedEtag(), "\"v8\"");
}

TEST(Check, BigImageStreamsInTwoKilobyteChunks) {
  Rig r;
  Options o;
  o.sha = v::kBigShaHex;
  o.signature = v::kBigSigProd;
  r.serve(body(o));
  r.http.image.clear();
  for (size_t i = 0; i < v::kBigImageBytes; ++i) r.http.image.push_back(static_cast<char>(v::kBigImageByte(i)));
  r.http.openResult = static_cast<int64_t>(r.http.image.size());
  const CheckResult res = r.run();
  ASSERT_EQ(res.outcome, Outcome::kInstalled);
  EXPECT_EQ(r.slots.written.size(), v::kBigImageBytes);
  EXPECT_EQ(r.http.readCaps, (std::vector<size_t>{2048, 2048, 904}));
}

TEST(Check, BadSignature) {
  Rig r;
  Options o;
  o.signature = v::kSigWrongKey;
  r.serve(body(o));
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kRejected);
  EXPECT_STREQ(res.reason, "bad_signature");
  EXPECT_TRUE(r.http.openedPaths.empty());
  EXPECT_EQ(r.storedEtag(), "");
}

TEST(Check, WrongKey) {
  Rig r;
  r.cfg.keys = KeyList{kOtherKey, 1};
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kRejected);
  EXPECT_STREQ(res.reason, "bad_signature");
  EXPECT_EQ(r.storedEtag(), "");
}

TEST(Check, RcManifestAtTheProdPathIsChannelNotAccepted) {
  Rig r;
  Options o;
  o.channel = "rc";
  o.signature = v::kSigRc;
  r.serve(body(o));
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kRejected);
  EXPECT_STREQ(res.reason, "channel_not_accepted");
}

TEST(Check, DowngradeIsRejectedServerRollback) {
  Rig r;
  r.cfg.runningBuild = 9;
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kRejected);
  EXPECT_STREQ(res.reason, "server_rollback");
  EXPECT_TRUE(r.http.openedPaths.empty());
}

TEST(Check, SameBuildIsNoUpdateAlreadyCurrent) {
  Rig r;
  r.cfg.runningBuild = 8;
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kNoUpdate);
  EXPECT_STREQ(res.reason, "already_current");
  EXPECT_TRUE(r.http.openedPaths.empty());
}

TEST(Check, CorruptImageIsHashMismatchAndAborts) {
  Rig r;
  r.http.image[3] ^= 0x01;
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kFailed);
  EXPECT_STREQ(res.reason, "hash_mismatch");
  expectAbortedWithoutActivation(r);
}

TEST(Check, TruncatedStreamIsDownloadIncompleteAndAborts) {
  Rig r;
  r.http.image.resize(10);  // the server promised 30 bytes, then the stream ends
  r.http.openResult = 30;
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kFailed);
  EXPECT_STREQ(res.reason, "download_incomplete");
  expectAbortedWithoutActivation(r);
}

TEST(Check, StallReadReturnsMinusOneIsDownloadIncomplete) {
  Rig r;
  r.http.imageLimit = 10;
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kFailed);
  EXPECT_STREQ(res.reason, "download_incomplete");
  expectAbortedWithoutActivation(r);
}

TEST(Check, WriteFailureAborts) {
  Rig r;
  r.slots.writeLimit = 5;
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kFailed);
  EXPECT_STREQ(res.reason, "write_failed");
  expectAbortedWithoutActivation(r);
}

TEST(Check, ImageLargerThanSlotIsBeginFailedAndNothingIsRead) {
  Rig r;
  r.slots.slotCapacity = 10;
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kFailed);
  EXPECT_STREQ(res.reason, "begin_failed");
  EXPECT_TRUE(r.http.readCaps.empty());
  EXPECT_EQ(r.slots.endCalls, 0);
  EXPECT_TRUE(r.http.closed);
  EXPECT_EQ(r.storedEtag(), "");
}

TEST(Check, ZeroLengthImageIsBeginFailedWithoutOpeningTheSlot) {
  Rig r;
  r.http.openResult = 0;
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kFailed);
  EXPECT_STREQ(res.reason, "begin_failed");
  EXPECT_FALSE(r.logged("beginUpdate"));
}

TEST(Check, ImageHttpErrorIsFetchFailed) {
  Rig r;
  r.http.openResult = -404;
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kFailed);
  EXPECT_STREQ(res.reason, "fetch_failed");
  EXPECT_EQ(r.storedEtag(), "");
  EXPECT_FALSE(r.logged("beginUpdate"));
}

TEST(Check, BadJsonIsMalformed) {
  Rig r;
  r.serve("this is not json");
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kRejected);
  EXPECT_STREQ(res.reason, "malformed");
  EXPECT_EQ(r.storedEtag(), "");
}

TEST(Check, OversizedManifestIsMalformed) {
  Rig r;
  FakeHttp::Manifest m;
  m.body = body();
  m.tooLarge = true;
  r.http.manifests[kManifestPath] = m;
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kRejected);
  EXPECT_STREQ(res.reason, "malformed");
}

TEST(Check, Http404IsFetchFailed) {
  Rig r;
  r.serve("", 404);
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kFailed);
  EXPECT_STREQ(res.reason, "fetch_failed");
  EXPECT_EQ(r.storedEtag(), "");
}

TEST(Check, TransportFailureIsFetchFailed) {
  Rig r;
  r.http.manifests.clear();
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kFailed);
  EXPECT_STREQ(res.reason, "fetch_failed");
}

TEST(Check, NotModifiedSkipsEverything) {
  Rig r;
  r.serve("", 304);
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kNotModified);
  EXPECT_STREQ(res.reason, "not_modified");
  EXPECT_TRUE(r.http.openedPaths.empty());
  EXPECT_EQ(r.store.writeCount, 0);
}

TEST(Check, IfNoneMatchSentFromStore) {
  Rig r;
  r.run();
  EXPECT_EQ(r.http.ifNoneMatchSent.front(), "");  // nothing stored yet
  r.http.ifNoneMatchSent.clear();
  r.cfg.runningBuild = 8;
  r.run();
  EXPECT_EQ(r.http.ifNoneMatchSent.front(), "\"v8\"");
}

TEST(Check, EtagStoredAfterNoUpdate) {
  Rig r;
  r.cfg.runningBuild = 8;
  r.run();
  EXPECT_EQ(r.storedEtag(), "\"v8\"");
}

TEST(Check, EtagStoredForAVerifiedRejection) {
  Rig r;
  r.cfg.runningBuild = 9;
  r.run();
  EXPECT_EQ(r.storedEtag(), "\"v8\"");
}

TEST(Check, NoEtagInTheResponseClearsAStoredOne) {
  Rig r;
  ASSERT_TRUE(OtaState(r.store).setEtag("\"old\""));
  r.cfg.runningBuild = 8;
  r.serve(body(), 200, "");
  r.run();
  EXPECT_EQ(r.storedEtag(), "");
}

TEST(Check, EtagNotStoredAfterFailedDownload) {
  Rig r;
  r.http.openResult = -1;
  r.run();
  EXPECT_EQ(r.storedEtag(), "");
}

TEST(Check, EtagNotStoredAfterTruncatedDownload) {
  Rig r;
  r.http.imageLimit = 3;
  r.run();
  EXPECT_EQ(r.storedEtag(), "");
}

TEST(Check, EtagNotStoredForBadSignature) {
  Rig r;
  r.cfg.keys = KeyList{kOtherKey, 1};
  r.run();
  EXPECT_EQ(r.storedEtag(), "");
}

TEST(Check, StaleEtagDoesNotSurviveAFailedDownload) {
  Rig r;
  ASSERT_TRUE(OtaState(r.store).setEtag("\"old\""));
  r.http.imageLimit = 3;
  r.run();
  EXPECT_EQ(r.storedEtag(), "\"old\"");  // untouched: the next check still refetches the image
}

TEST(Check, BadPathNeverOpensImage) {
  Rig r;
  Options o;
  o.firmware = "firmware.bin";
  r.serve(body(o));
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kRejected);
  EXPECT_STREQ(res.reason, "bad_path");
  EXPECT_TRUE(r.http.openedPaths.empty());
}

TEST(Check, BlacklistedBuildRejected) {
  Rig r;
  ASSERT_TRUE(OtaState(r.store).blacklist(8));
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kRejected);
  EXPECT_STREQ(res.reason, "blacklisted");
  EXPECT_TRUE(r.http.openedPaths.empty());
}

TEST(Check, BelowFloorRejected) {
  Rig r;
  r.store.uints["floor"] = 9;
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kRejected);
  EXPECT_STREQ(res.reason, "below_min_build");
}

TEST(Check, FloorNeverRaisedByCheck) {
  Rig r;
  r.store.uints["floor"] = 4;
  ASSERT_EQ(r.run().outcome, Outcome::kInstalled);
  EXPECT_EQ(r.store.uints["floor"], 4u);
}

TEST(Check, NotInRolloutIsNoUpdate) {
  // This board's bucket is 77 (first two id bytes 0x6ed9 = 28377, mod 100).
  Rig r;
  Options o;
  o.rollout = "0";
  r.serve(body(o));
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kNoUpdate);
  EXPECT_STREQ(res.reason, "not_in_rollout");
  EXPECT_TRUE(r.http.openedPaths.empty());
}

TEST(Check, RolloutBoundaryAtTheBoardsBucket) {
  Rig r;
  Options o;
  o.rollout = "77";
  r.serve(body(o));
  EXPECT_EQ(r.run().outcome, Outcome::kNoUpdate);  // bucket 77 is not < 77
  Rig in;
  o.rollout = "78";
  in.serve(body(o));
  EXPECT_EQ(in.run().outcome, Outcome::kInstalled);
}

TEST(Check, StoreNotOkFailsClosedWithoutHttp) {
  Rig r;
  r.store.okValue = false;
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kFailed);
  EXPECT_STREQ(res.reason, "store_unavailable");
  EXPECT_TRUE(r.http.fetchedPaths.empty());
  EXPECT_EQ(r.store.writeCount, 0);
}

TEST(Check, PendingWriteFailureAbortsBeforeActivation) {
  Rig r;
  r.store.failWrites = true;
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kFailed);
  EXPECT_STREQ(res.reason, "store_unavailable");
  EXPECT_EQ(r.slots.endCalls, 0);
  EXPECT_TRUE(r.logged("abortUpdate"));
}

TEST(Check, EndUpdateFailureClearsPending) {
  Rig r;
  r.slots.endFails = true;
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kFailed);
  EXPECT_STREQ(res.reason, "end_failed");
  EXPECT_EQ(r.slots.endCalls, 1);
  EXPECT_EQ(r.store.uints.count("pending"), 0u);
  EXPECT_LT(r.indexOf("endUpdate"), r.indexOf("erase:pending"));
  EXPECT_EQ(r.storedEtag(), "");
}

TEST(Check, AbortNeverActivates) {
  const char* names[] = {"hash", "truncated", "stall", "write"};
  for (const char* name : names) {
    Rig r;
    const std::string n = name;
    if (n == "hash") r.http.image[0] ^= 0x01;
    if (n == "truncated") r.http.image.resize(4), r.http.openResult = 30;
    if (n == "stall") r.http.imageLimit = 2;
    if (n == "write") r.slots.writeLimit = 1;
    EXPECT_EQ(r.run().outcome, Outcome::kFailed) << name;
    EXPECT_EQ(r.slots.endCalls, 0) << name;
    EXPECT_FALSE(r.logged("endUpdate")) << name;
  }
}

TEST(Check, BasePathTooLongForTheManifestPathIsFetchFailed) {
  Rig r;
  const std::string longBase(100, 'a');
  r.cfg.basePath = longBase.c_str();
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kFailed);
  EXPECT_STREQ(res.reason, "fetch_failed");
  EXPECT_TRUE(r.http.fetchedPaths.empty());
}
