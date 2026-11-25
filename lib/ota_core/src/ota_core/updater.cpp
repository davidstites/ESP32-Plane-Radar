#include "ota_core/updater.h"

#include <cstdio>
#include <cstring>

#include "ota_core/policy.h"
#include "ota_core/sha256.h"
#include "ota_core/signature.h"

namespace ota_core {

namespace {

CheckResult result(Outcome outcome, const char* reason, uint32_t build = 0) {
  return CheckResult{outcome, reason, build, false};
}

}  // namespace

BootInfo Updater::onBoot() {
  BootInfo info{};
  info.pendingVerify = slots_.runningState() == SlotState::kPendingVerify;

  // The manifest ETag answers for the build that was running when it was stored. If a
  // different build is running now (a USB flash keeps NVS), forget it or 304 would hide
  // updates forever.
  if (state_.bootedBuild() != cfg_.runningBuild) {
    state_.clearEtag();
    state_.setBootedBuild(cfg_.runningBuild);
  }

  const uint32_t pending = state_.pending();
  if (pending != 0 && pending != cfg_.runningBuild) {
    if (slots_.hasInvalidSlot() && state_.blacklist(pending)) {
      info.rolledBack = true;
      info.rolledBackBuild = pending;
    }
    state_.clearPending();
  }
  return info;
}

bool Updater::confirmBoot() {
  if (slots_.runningState() != SlotState::kPendingVerify) return true;
  if (!state_.ok() || !slots_.markRunningValid()) return false;
  const bool raised = state_.raiseFloor(cfg_.runningBuild);
  const bool cleared = state_.clearPending();
  return raised && cleared;
}

void Updater::rememberEtag(const char* etag) {
  if (etag[0] != '\0') {
    state_.setEtag(etag);
  } else {
    state_.clearEtag();
  }
}

CheckResult Updater::fetchAndInstall(const char* firmware, const uint8_t sha[kShaBytes], uint32_t build,
                                     bool candidate) {
  const int64_t length = http_.openImage(firmware);
  CheckResult res = result(Outcome::kFailed, "fetch_failed", build);
  if (length == 0) {
    res = result(Outcome::kFailed, "begin_failed", build);
  } else if (length > 0) {
    res = install(sha, build, static_cast<size_t>(length));
  }
  http_.closeImage();
  res.candidate = candidate && res.outcome == Outcome::kInstalled;
  return res;
}

// Fetch and vet this board's release-candidate manifest. Any problem means "no candidate":
// the caller falls back to production. Overwrites response_ and manifest_.
bool Updater::fetchCandidate(const char* ownHash, VerifiedOffer& offer, char* firmware) {
  char path[kPathMax + 64];
  if (!formatRcPath(cfg_.basePath, ownHash, path, sizeof(path))) return false;
  memset(&response_, 0, sizeof(response_));
  if (!http_.fetchManifest(path, nullptr, response_)) return false;
  if (response_.status != 200 || response_.tooLarge) return false;
  if (parseManifest(response_.body, response_.bodyLen, manifest_) != ParseError::kNone) return false;
  if (verifyManifest(manifest_.fields(), channelBit(Channel::kRc), cfg_.deviceId, cfg_.keys, cfg_.verify,
                     offer) != SigError::kNone) {
    return false;
  }
  if (!isAllowedPath(manifest_.firmware)) return false;
  // Candidates ignore the rollout percentage: being listed is the opt-in.
  const LocalState local{cfg_.runningBuild, state_.floor(), state_.isBlacklisted(offer.build), true};
  if (evaluateOffer(Offer{offer.build, offer.minBuild}, local) != Verdict::kAccept) return false;
  strcpy(firmware, manifest_.firmware);
  return true;
}

CheckResult Updater::check() {
  if (!state_.ok()) return result(Outcome::kFailed, "store_unavailable");

  char path[kPathMax + 1];
  const int plen = snprintf(path, sizeof(path), "%s/manifest.json", cfg_.basePath);
  if (plen < 0 || static_cast<size_t>(plen) >= sizeof(path)) return result(Outcome::kFailed, "fetch_failed");

  // A listed board must see the prod manifest every time: its candidate manifest can change
  // while the prod manifest does not, and a 304 would hide that.
  char etag[OtaState::kEtagMax + 1];
  const bool sendEtag = !state_.rcListed() && state_.etag(etag, sizeof(etag)) > 0;
  memset(&response_, 0, sizeof(response_));
  if (!http_.fetchManifest(path, sendEtag ? etag : nullptr, response_)) {
    return result(Outcome::kFailed, "fetch_failed");
  }
  if (response_.status == 304) return result(Outcome::kNotModified, "not_modified");
  if (response_.status != 200) return result(Outcome::kFailed, "fetch_failed");
  if (response_.tooLarge) return result(Outcome::kRejected, "malformed");

  if (parseManifest(response_.body, response_.bodyLen, manifest_) != ParseError::kNone) {
    return result(Outcome::kRejected, "malformed");
  }
  VerifiedOffer prod{};
  const SigError sig = verifyManifest(manifest_.fields(), channelBit(Channel::kProd), cfg_.deviceId,
                                      cfg_.keys, cfg_.verify, prod);
  if (sig != SigError::kNone) return result(Outcome::kRejected, sigErrorName(sig));

  // From here the signed fields are authentic. Keep what the candidate fetch would overwrite.
  char prodEtag[sizeof(response_.etag)];
  char prodFirmware[kPathMax + 1];
  memcpy(prodEtag, response_.etag, sizeof(prodEtag));
  memcpy(prodFirmware, manifest_.firmware, sizeof(prodFirmware));
  const char* candidates[kMaxCandidates];
  for (size_t i = 0; i < manifest_.candidateCount; ++i) candidates[i] = manifest_.candidates[i];
  char ownHash[33];
  deviceHashHex(cfg_.deviceId, ownHash);
  const bool listed = isListed(candidates, manifest_.candidateCount, ownHash);
  state_.setRcListed(listed);

  CheckResult prodResult = result(Outcome::kRejected, "bad_path", prod.build);
  bool prodAccepted = false;
  if (isAllowedPath(prodFirmware)) {
    const LocalState local{cfg_.runningBuild, state_.floor(), state_.isBlacklisted(prod.build),
                           inRolloutGroup(cfg_.deviceId, manifest_.rollout)};
    const Verdict verdict = evaluateOffer(Offer{prod.build, prod.minBuild}, local);
    prodAccepted = verdict == Verdict::kAccept;
    const bool quiet = verdict == Verdict::kAlreadyCurrent || verdict == Verdict::kNotInRollout;
    prodResult = result(quiet ? Outcome::kNoUpdate : Outcome::kRejected, verdictName(verdict), prod.build);
  }

  VerifiedOffer rc{};
  char rcFirmware[kPathMax + 1];
  const bool rcAccepted = listed && fetchCandidate(ownHash, rc, rcFirmware);

  CheckResult final = prodResult;
  switch (chooseInstall(prodAccepted, prod.build, rcAccepted, rc.build)) {
    case InstallChoice::kCandidate:
      final = fetchAndInstall(rcFirmware, rc.sha256, rc.build, true);
      break;
    case InstallChoice::kProduction:
      final = fetchAndInstall(prodFirmware, prod.sha256, prod.build, false);
      break;
    case InstallChoice::kNone:
      break;
  }
  if (final.outcome != Outcome::kFailed) rememberEtag(prodEtag);
  return final;
}

CheckResult Updater::install(const uint8_t sha[kShaBytes], uint32_t build, size_t length) {
  if (!slots_.beginUpdate(length)) return result(Outcome::kFailed, "begin_failed", build);

  Sha256 hash;
  hash.start();
  size_t remaining = length;
  while (remaining > 0) {
    const size_t want = remaining < kChunkBytes ? remaining : kChunkBytes;
    const int got = http_.readImage(chunk_, want);
    if (got <= 0) {
      slots_.abortUpdate();
      return result(Outcome::kFailed, "download_incomplete", build);
    }
    const size_t n = static_cast<size_t>(got);
    if (slots_.writeUpdate(chunk_, n) != n) {
      slots_.abortUpdate();
      return result(Outcome::kFailed, "write_failed", build);
    }
    hash.update(chunk_, n);
    remaining -= n;
  }

  uint8_t digest[kShaBytes];
  hash.finish(digest);
  if (memcmp(digest, sha, kShaBytes) != 0) {
    slots_.abortUpdate();
    return result(Outcome::kFailed, "hash_mismatch", build);
  }

  // Pending goes to NVS before the slot is activated, so a reboot right after endUpdate
  // still finds a record to reconcile against.
  if (!state_.setPending(build)) {
    slots_.abortUpdate();
    return result(Outcome::kFailed, "store_unavailable", build);
  }
  if (!slots_.endUpdate()) {
    state_.clearPending();
    return result(Outcome::kFailed, "end_failed", build);
  }
  return result(Outcome::kInstalled, "installed", build);
}

}  // namespace ota_core
