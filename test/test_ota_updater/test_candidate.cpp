#include <gtest/gtest.h>

#include "rig.h"

using namespace rig;

namespace {

const char kOwnHash[] = "6ed9777f98c4a6e271181c6b16b87843";  // first 16 bytes of v::kDeviceId
const char kRcPath[] = "/planeradar/rc/6ed9777f98c4a6e271181c6b16b87843.json";
const char kProdFirmware[] = "/planeradar/firmware-8.bin";
const char kRcFirmware[] = "/planeradar/rc-9.bin";

std::string listing(const std::string& hash = kOwnHash) { return "[\"" + hash + "\"]"; }

Options prodListing() {
  Options o;
  o.candidates = listing();
  return o;
}

// An rc manifest for the 5000-byte image: build 9, signed for this board.
Options bigRc() {
  Options o;
  o.sha = v::kBigShaHex;
  o.signature = v::kBigSigRc;
  o.channel = "rc";
  o.firmware = kRcFirmware;
  o.build = "9";
  return o;
}

void serveRc(Rig& r, const Options& o, int status = 200) {
  FakeHttp::Manifest m;
  m.status = status;
  m.body = body(o);
  r.http.manifests[kRcPath] = m;
}

// A rig whose prod manifest lists this board and whose rc manifest offers the big image.
Rig listedRig() {
  Rig r;
  r.serve(body(prodListing()));
  serveRc(r, bigRc());
  r.http.image.clear();
  for (size_t i = 0; i < v::kBigImageBytes; ++i) r.http.image.push_back(static_cast<char>(v::kBigImageByte(i)));
  r.http.openResult = static_cast<int64_t>(r.http.image.size());
  return r;
}

}  // namespace

TEST(Candidate, ListedBoardFetchesItsOwnRcManifestAtTheExactPath) {
  Rig r = listedRig();
  r.run();
  ASSERT_EQ(r.http.fetchedPaths.size(), 2u);
  EXPECT_EQ(r.http.fetchedPaths[0], "/planeradar/manifest.json");
  EXPECT_EQ(r.http.fetchedPaths[1], kRcPath);
  EXPECT_EQ(r.http.ifNoneMatchSent[1], "");  // the rc request never carries an ETag
}

TEST(Candidate, UnlistedBoardNeverFetchesRc) {
  Rig r;
  Options o;
  o.candidates = listing("ffffffffffffffffffffffffffffffff");
  r.serve(body(o));
  r.run();
  EXPECT_EQ(r.http.fetchedPaths, std::vector<std::string>{"/planeradar/manifest.json"});
}

TEST(Candidate, ProdWithoutAnyCandidatesNeverFetchesRc) {
  Rig r;
  r.run();
  EXPECT_EQ(r.http.fetchedPaths.size(), 1u);
}

TEST(Candidate, Rc404FallsBackToProd) {
  Rig r;
  r.serve(body(prodListing()));
  FakeHttp::Manifest gone;
  gone.status = 404;
  r.http.manifests[kRcPath] = gone;
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kInstalled);
  EXPECT_FALSE(res.candidate);
  EXPECT_EQ(res.build, 8u);
  EXPECT_EQ(r.http.openedPaths, std::vector<std::string>{kProdFirmware});
}

TEST(Candidate, RcTransportFailureFallsBackToProd) {
  Rig r;
  r.serve(body(prodListing()));  // no rc manifest registered: the fake reports a transport failure
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kInstalled);
  EXPECT_FALSE(res.candidate);
}

TEST(Candidate, RcWithProdSignatureRejected) {
  Rig r = listedRig();
  Options o;  // prod signature, but served as an rc manifest
  o.channel = "rc";
  serveRc(r, o);
  r.http.image = kImage;
  r.http.openResult = static_cast<int64_t>(r.http.image.size());
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kInstalled);
  EXPECT_FALSE(res.candidate);  // the bad candidate was ignored; prod installed
  EXPECT_EQ(r.http.openedPaths, std::vector<std::string>{kProdFirmware});
}

TEST(Candidate, ProdChannelAtTheRcPathRejected) {
  Rig r = listedRig();
  serveRc(r, Options());  // channel "prod" with a prod signature, at the rc path
  r.http.image = kImage;
  r.http.openResult = static_cast<int64_t>(r.http.image.size());
  const CheckResult res = r.run();
  EXPECT_FALSE(res.candidate);
  EXPECT_EQ(r.http.openedPaths, std::vector<std::string>{kProdFirmware});
}

TEST(Candidate, RcSignedForAnotherBoardRejected) {
  Rig r = listedRig();
  Options o;
  o.channel = "rc";
  o.signature = v::kSigRcOtherBoard;
  serveRc(r, o);
  r.http.image = kImage;
  r.http.openResult = static_cast<int64_t>(r.http.image.size());
  const CheckResult res = r.run();
  EXPECT_FALSE(res.candidate);
  EXPECT_EQ(r.http.openedPaths, std::vector<std::string>{kProdFirmware});
}

TEST(Candidate, MalformedRcDoesNotBlockProd) {
  Rig r = listedRig();
  FakeHttp::Manifest bad;
  bad.body = "not json";
  r.http.manifests[kRcPath] = bad;
  r.http.image = kImage;
  r.http.openResult = static_cast<int64_t>(r.http.image.size());
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kInstalled);
  EXPECT_FALSE(res.candidate);
}

TEST(Candidate, OversizedRcDoesNotBlockProd) {
  Rig r = listedRig();
  FakeHttp::Manifest big;
  big.body = body(bigRc());
  big.tooLarge = true;
  r.http.manifests[kRcPath] = big;
  r.http.image = kImage;
  r.http.openResult = static_cast<int64_t>(r.http.image.size());
  EXPECT_FALSE(r.run().candidate);
}

TEST(Candidate, RcWithBadPathDoesNotBlockProd) {
  Rig r = listedRig();
  Options o = bigRc();
  o.firmware = "//evil/x";
  serveRc(r, o);
  r.http.image = kImage;
  r.http.openResult = static_cast<int64_t>(r.http.image.size());
  const CheckResult res = r.run();
  EXPECT_FALSE(res.candidate);
  EXPECT_EQ(r.http.openedPaths, std::vector<std::string>{kProdFirmware});
}

TEST(Candidate, RcNewerThanProdInstalledAsCandidate) {
  Rig r = listedRig();
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kInstalled);
  EXPECT_STREQ(res.reason, "installed");
  EXPECT_TRUE(res.candidate);
  EXPECT_EQ(res.build, 9u);
  EXPECT_EQ(r.http.openedPaths, std::vector<std::string>{kRcFirmware});
  EXPECT_EQ(r.store.uints["pending"], 9u);
  EXPECT_EQ(r.slots.written.size(), v::kBigImageBytes);
}

TEST(Candidate, RcInstallsEvenWhenProdIsAlreadyCurrent) {
  Rig r = listedRig();
  r.cfg.runningBuild = 8;  // prod build 8 is current, rc build 9 is newer
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kInstalled);
  EXPECT_TRUE(res.candidate);
}

TEST(Candidate, RcOlderThanProdIgnored) {
  Rig r = listedRig();
  Options o;
  o.channel = "rc";
  o.signature = v::kSigOldRc;
  o.build = "7";
  serveRc(r, o);
  r.http.image = kImage;
  r.http.openResult = static_cast<int64_t>(r.http.image.size());
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kInstalled);
  EXPECT_FALSE(res.candidate);
  EXPECT_EQ(res.build, 8u);
}

TEST(Candidate, RcEqualToProdInstallsProd) {
  Rig r = listedRig();
  Options o;
  o.channel = "rc";
  o.signature = v::kSigRc;  // build 8, same as prod
  serveRc(r, o);
  r.http.image = kImage;
  r.http.openResult = static_cast<int64_t>(r.http.image.size());
  const CheckResult res = r.run();
  EXPECT_FALSE(res.candidate);
  EXPECT_EQ(r.http.openedPaths, std::vector<std::string>{kProdFirmware});
}

TEST(Candidate, PromotedBuildIsAlreadyCurrentOnTheRcBoard) {
  Rig r = listedRig();
  ASSERT_TRUE(r.run().candidate);
  // Promotion: prod now serves the same build and bytes under the prod signature, and the
  // board is still listed. The board reboots into build 9.
  Options prod;
  prod.sha = v::kBigShaHex;
  prod.signature = v::kBigSigProdBuild9;
  prod.build = "9";
  prod.candidates = listing();
  r.cfg.runningBuild = 9;
  r.serve(body(prod));
  r.http.openedPaths.clear();
  const CheckResult again = r.run();
  EXPECT_EQ(again.outcome, Outcome::kNoUpdate);
  EXPECT_STREQ(again.reason, "already_current");
  EXPECT_TRUE(r.http.openedPaths.empty());
}

TEST(Candidate, BlacklistedRcRejected) {
  Rig r = listedRig();
  ASSERT_TRUE(OtaState(r.store).blacklist(9));
  r.http.image = kImage;  // prod build 8 installs the small image
  r.http.openResult = static_cast<int64_t>(r.http.image.size());
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kInstalled);
  EXPECT_FALSE(res.candidate);  // prod build 8 went in instead of the blacklisted rc build 9
  EXPECT_EQ(res.build, 8u);
}

TEST(Candidate, RcBelowTheFloorRejected) {
  Rig r = listedRig();
  r.store.uints["floor"] = 10;
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kRejected);
  EXPECT_STREQ(res.reason, "below_min_build");
}

TEST(Candidate, RcIgnoresTheRolloutPercentage) {
  Rig r = listedRig();
  Options o = bigRc();
  o.rollout = "0";
  serveRc(r, o);
  EXPECT_TRUE(r.run().candidate);
}

TEST(Candidate, RcDownloadFailureLeavesProdUntouchedAndNoEtag) {
  Rig r = listedRig();
  r.http.imageLimit = 100;
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kFailed);
  EXPECT_STREQ(res.reason, "download_incomplete");
  EXPECT_EQ(r.http.openedPaths, std::vector<std::string>{kRcFirmware});  // prod was not tried
  EXPECT_EQ(r.slots.endCalls, 0);
  EXPECT_EQ(r.storedEtag(), "");
  EXPECT_EQ(r.store.uints.count("pending"), 0u);
}

TEST(Candidate, ProdRejectionStillStopsBeforeAnyRcFetch) {
  Rig r;
  Options o = prodListing();
  o.signature = v::kSigWrongKey;
  r.serve(body(o));
  const CheckResult res = r.run();
  EXPECT_EQ(res.outcome, Outcome::kRejected);
  EXPECT_EQ(r.http.fetchedPaths.size(), 1u);
}

TEST(Candidate, EtagSurvivesACandidateInstall) {
  Rig r = listedRig();
  ASSERT_TRUE(r.run().candidate);
  EXPECT_EQ(r.storedEtag(), "\"v8\"");  // the prod response's ETag, not an rc one
}

TEST(Candidate, ListedBoardRefetchesProdWithoutAnEtag) {
  // A new rc build can appear without the prod manifest changing, so a listed board must
  // not let a 304 skip the candidate check.
  Rig r = listedRig();
  r.cfg.runningBuild = 9;  // nothing to install; the check just records the listing
  r.run();
  r.http.ifNoneMatchSent.clear();
  r.run();
  EXPECT_EQ(r.http.ifNoneMatchSent.front(), "");
}

TEST(Candidate, UnlistedBoardResumesSendingTheEtag) {
  Rig r = listedRig();
  r.cfg.runningBuild = 9;
  r.run();                // listed: no ETag from here on
  r.serve(body());        // the board is delisted
  r.cfg.runningBuild = 8;
  r.run();                // records "not listed" and stores the ETag
  r.http.ifNoneMatchSent.clear();
  r.run();
  EXPECT_EQ(r.http.ifNoneMatchSent.front(), "\"v8\"");
}

TEST(Candidate, ListedFlagIsOnlyWrittenWhenItChanges) {
  Rig r = listedRig();
  r.cfg.runningBuild = 9;
  r.run();
  r.run();
  EXPECT_EQ(std::count(r.log.begin(), r.log.end(), "putUInt:rclisted"), 1);
  EXPECT_EQ(r.store.uints["rclisted"], 1u);
}
