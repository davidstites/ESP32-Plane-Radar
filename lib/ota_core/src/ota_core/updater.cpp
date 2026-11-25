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

void Updater::rememberEtag() {
  if (response_.etag[0] != '\0') {
    state_.setEtag(response_.etag);
  } else {
    state_.clearEtag();
  }
}

CheckResult Updater::check() {
  if (!state_.ok()) return result(Outcome::kFailed, "store_unavailable");

  char path[kPathMax + 1];
  const int plen = snprintf(path, sizeof(path), "%s/manifest.json", cfg_.basePath);
  if (plen < 0 || static_cast<size_t>(plen) >= sizeof(path)) return result(Outcome::kFailed, "fetch_failed");

  char etag[OtaState::kEtagMax + 1];
  const bool haveEtag = state_.etag(etag, sizeof(etag)) > 0;
  memset(&response_, 0, sizeof(response_));
  if (!http_.fetchManifest(path, haveEtag ? etag : nullptr, response_)) {
    return result(Outcome::kFailed, "fetch_failed");
  }
  if (response_.status == 304) return result(Outcome::kNotModified, "not_modified");
  if (response_.status != 200) return result(Outcome::kFailed, "fetch_failed");
  if (response_.tooLarge) return result(Outcome::kRejected, "malformed");

  if (parseManifest(response_.body, response_.bodyLen, manifest_) != ParseError::kNone) {
    return result(Outcome::kRejected, "malformed");
  }
  VerifiedOffer offer{};
  const SigError sig = verifyManifest(manifest_.fields(), channelBit(Channel::kProd), cfg_.deviceId,
                                      cfg_.keys, cfg_.verify, offer);
  if (sig != SigError::kNone) return result(Outcome::kRejected, sigErrorName(sig));

  // From here the signed fields are authentic, so the response may be remembered.
  if (!isAllowedPath(manifest_.firmware)) {
    rememberEtag();
    return result(Outcome::kRejected, "bad_path", offer.build);
  }
  const LocalState local{cfg_.runningBuild, state_.floor(), state_.isBlacklisted(offer.build),
                         inRolloutGroup(cfg_.deviceId, manifest_.rollout)};
  const Verdict verdict = evaluateOffer(Offer{offer.build, offer.minBuild}, local);
  if (verdict != Verdict::kAccept) {
    rememberEtag();
    const bool quiet = verdict == Verdict::kAlreadyCurrent || verdict == Verdict::kNotInRollout;
    return result(quiet ? Outcome::kNoUpdate : Outcome::kRejected, verdictName(verdict), offer.build);
  }

  const char* firmware = manifest_.firmware;
  const int64_t length = http_.openImage(firmware);
  if (length < 0) {
    http_.closeImage();
    return result(Outcome::kFailed, "fetch_failed", offer.build);
  }
  CheckResult installed = result(Outcome::kFailed, "begin_failed", offer.build);
  if (length > 0) installed = install(offer.sha256, offer.build, static_cast<size_t>(length));
  http_.closeImage();
  if (installed.outcome == Outcome::kInstalled) rememberEtag();
  return installed;
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
