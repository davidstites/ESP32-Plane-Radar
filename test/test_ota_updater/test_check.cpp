#include <gtest/gtest.h>

#include "rig.h"

using namespace rig;


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
  EXPECT_EQ(r.store.uints["activated"], 8u);
  EXPECT_LT(r.indexOf("endUpdate"), r.indexOf("putUInt:activated"));
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

TEST(Check, RefusesToTouchTheSlotsWhileTheRunningImageIsUnconfirmed) {
  Rig r;
  r.slots.state = SlotState::kPendingVerify;
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kFailed);
  EXPECT_STREQ(res.reason, "pending_verify");
  EXPECT_TRUE(r.http.fetchedPaths.empty());
  EXPECT_EQ(r.indexOf("beginUpdate"), r.log.size());
  EXPECT_EQ(r.storedEtag(), "");
}

TEST(Check, AnHttpLayerReturningMoreThanAskedIsDownloadIncomplete) {
  Rig r;
  r.http.overReturn = 1;
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kFailed);
  EXPECT_STREQ(res.reason, "download_incomplete");
  expectAbortedWithoutActivation(r);
}

TEST(Check, AServerSideBadImageBacksOffInsteadOfRedownloadingEveryRetry) {
  Rig r;
  r.http.image = "planeradar test firmware imagX";  // right length, wrong bytes: hash_mismatch
  Updater u(r.cfg, r.store, r.slots, r.http);
  EXPECT_STREQ(u.check().reason, "hash_mismatch");
  EXPECT_STREQ(u.check().reason, "hash_mismatch");
  for (int i = 0; i < 8; ++i) {
    const CheckResult skipped = u.check();
    EXPECT_EQ(skipped.outcome, Outcome::kFailed);
    EXPECT_STREQ(skipped.reason, "backoff");
  }
  EXPECT_EQ(r.http.openedPaths.size(), 2u);  // no download while backing off
  r.http.image = kImage;                     // the server fixed the image
  const CheckResult fixed = u.check();
  EXPECT_EQ(fixed.outcome, Outcome::kInstalled);
  EXPECT_EQ(r.http.openedPaths.size(), 3u);
}

TEST(Check, ABeginFailureAlsoBacksOff) {
  Rig r;
  r.slots.beginFails = true;
  Updater u(r.cfg, r.store, r.slots, r.http);
  EXPECT_STREQ(u.check().reason, "begin_failed");
  EXPECT_STREQ(u.check().reason, "begin_failed");
  EXPECT_STREQ(u.check().reason, "backoff");
}

TEST(Check, TransientDownloadFailuresNeverBackOff) {
  Rig r;
  r.http.imageLimit = 10;  // stalls mid-stream every time
  Updater u(r.cfg, r.store, r.slots, r.http);
  for (int i = 0; i < 4; ++i) EXPECT_STREQ(u.check().reason, "download_incomplete");
}

TEST(Check, ADifferentBadImageStartsItsOwnCount) {
  Rig r;
  r.http.image = "planeradar test firmware imagX";
  Updater u(r.cfg, r.store, r.slots, r.http);
  u.check();
  u.check();
  EXPECT_STREQ(u.check().reason, "backoff");
  Options o;
  o.sha = v::kBigShaHex;
  o.signature = v::kBigSigProd;
  r.serve(body(o));
  r.http.image = std::string(v::kBigImageBytes, 'x');  // also wrong: a fresh hash_mismatch
  r.http.openResult = static_cast<int64_t>(r.http.image.size());
  EXPECT_STREQ(u.check().reason, "hash_mismatch");
}

TEST(Probe, SucceedsOnAnyHttp200FromTheManifestPath) {
  Rig r;
  Updater u(r.cfg, r.store, r.slots, r.http);
  EXPECT_TRUE(u.probe());
  EXPECT_EQ(r.http.fetchedPaths, std::vector<std::string>{kManifestPath});
  EXPECT_EQ(r.http.ifNoneMatchSent, std::vector<std::string>{""});  // never a 304 shortcut
  EXPECT_TRUE(r.http.openedPaths.empty());                          // and never an install
  EXPECT_EQ(r.indexOf("beginUpdate"), r.log.size());
}

TEST(Probe, DoesNotCareWhatTheManifestSays) {
  Rig r;
  r.serve("not even json");
  Updater u(r.cfg, r.store, r.slots, r.http);
  EXPECT_TRUE(u.probe());  // it proves the TLS path and the host, nothing else
}

TEST(Probe, FailsOnTransportErrorOrNon200) {
  Rig r;
  r.http.manifests[kManifestPath].transportFails = true;
  Updater u(r.cfg, r.store, r.slots, r.http);
  EXPECT_FALSE(u.probe());
  r.http.manifests[kManifestPath].transportFails = false;
  r.serve(body(), 404);
  EXPECT_FALSE(u.probe());
  r.serve(body(), 304);
  EXPECT_FALSE(u.probe());
}

TEST(Probe, FailsClosedWhenTheStoreIsUnavailable) {
  Rig r;
  r.store.okValue = false;
  Updater u(r.cfg, r.store, r.slots, r.http);
  EXPECT_FALSE(u.probe());
  EXPECT_TRUE(r.http.fetchedPaths.empty());
}

TEST(Probe, BasePathTooLongForTheManifestPathFails) {
  Rig r;
  const std::string longBase(kPathMax, 'a');
  r.cfg.basePath = longBase.c_str();
  Updater u(r.cfg, r.store, r.slots, r.http);
  EXPECT_FALSE(u.probe());
}
