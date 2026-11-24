# OTA Updater Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Boards update themselves from a signed manifest, roll back automatically when a new image fails, and can be given a release candidate that was tested on real hardware before the identical bytes ship to everyone.

**Architecture:** A pure, host-tested `lib/ota_core` (signature verification, policy, manifest parsing, persisted state, updater state machine over three small HAL interfaces) plus `lib/crash_breadcrumb`. Firmware adapters wrap `Preferences`, `esp_ota_*`/`Update`, and `HTTPClient`. The existing single network task gets a new `kCheckOta` action, so one TLS session is ever open and the ADS-B connection is closed first. Rollback is the bootloader's own (`verifyRollbackLater()` + explicit confirm), proven by the spike.

**Tech Stack:** PlatformIO (espressif32@6.5.0, Arduino), GoogleTest native env, ArduinoJson 7, rweather/Crypto (`Ed25519::verify`), mbedtls SHA-256 on target, Python 3 + `cryptography` for release tooling.

**Spec:** No separate spec. Scope was agreed with David on 2026-11-24 and the constraints were measured by the spike on branch `ota-spike` (reference only, never merged). Both are captured in "Decisions" and "Global Constraints" below.

## Decisions

- **Keep:** signed manifests, anti-rollback floor raised only by confirm-boot, automatic rollback, staged rollout, persisted blacklist, ETag, crash breadcrumbs, release candidates.
- **Drop:** dev tickets (and therefore the clock), diagnostic profiles, boot-button rollback, custom OTA URL setting, variants (single product).
- **Release candidates:** the exact bytes tested on David's board are promoted by signing the same `sha256` again as `prod`. A board that ran RC build N sees prod build N as `already_current`.
- **Hosting:** default `https://metarly.app` with base path `/planeradar`; David confirms before Task 11.
- **Schedule:** first check 5 min after boot, then every 6 h plus 0-6 h random jitter, retry 30 min after a failed check. Uptime (`millis()`) only; no NTP, no wall clock.

## Global Constraints

- 100% line coverage of `lib/` (`scripts/coverage.sh` gate); new libs must be Arduino-free.
- No OCMock; fakes are subclasses whose methods are overridden by `std::function` members each test sets (`test/support/ota_fakes.h`). No inverted GoogleTest/XCTest-style expectations or timing-based waits.
- Warning-free builds (`pio run -e supermini`, `pio test -e native`).
- Work on branch `feature/ota-updater` from `main`; PR only, never push to `main`; commit trailer `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.
- Signed payload (91 bytes): `"PLANERADAR-OTA-v1\0"` (18) || sha256 (32) || build (4, BE) || min_build (4, BE) || channel (1: prod=0x01, rc=0x02) || device_id (32). `device_id` is 32 zero bytes for prod and the board's own id for rc; never read from the manifest.
- `device_id = SHA-256("PLANERADAR-DEVID-v1\0" || factory MAC (6 bytes))`; published form is the first 16 bytes as 32 lowercase hex characters; the MAC is never published.
- Keys: three 32-byte Ed25519 public-key slots (primary, secondary, emergency); an all-zero slot is disabled; `kPrimary` ships zeroed until a release key exists. Private keys never enter the repo; a metarmap key is never reused.
- Flash: `firmware.bin` must fit one `0x1F0000` (2,031,616 B) slot; baseline is 1,778,912 B and the spike's OTA code added about 26 KB.
- Heap/stack: one TLS session costs about 56 KB and fragments the largest block to about 11 KB. OTA runs on the existing `net` task (16 KB stack) with `adsb::closeConnection()` called first, exactly like `refreshFeeds()`. Verify costs about 108 ms and 1.6 KB of stack.
- Manifest body cap 1024 bytes; build values above 999999 are rejected; `firmware` must be an absolute path on the same host.
- Never publish or upload anything automatically; release tooling that uploads requires `--yes`.

## Review Focus

1. A failed or truncated download must not poison the ETag, or the next check gets `304` and never retries. (Task 8)
2. `firmware` paths with `..`, `//`, a scheme or host, a query, or odd characters must be refused before any request. (Task 4)
3. A store that fails to open must fail closed: no install, no floor write. (Task 6, Task 8)
4. More than 8 blacklisted builds must evict the oldest, not corrupt the set or refuse to record the newest. (Task 6)
5. The spike never ran an update while the radar render loop was running; heap and stack during a real in-worker check are unmeasured. (Task 13)

## File Structure

| File | Responsibility |
|---|---|
| `lib/ota_core/src/ota_core/types.h` | `Ed25519VerifyFn`, `KeyList`, `Channel`, size constants |
| `.../signature.{h,cpp}` | base64 signature decode, device id, payload, `verifyManifest` |
| `.../keys.h` | the three public-key slots, `isDisabled` |
| `.../sha256.{h,cpp}` | streaming SHA-256 (portable on host, mbedtls on target) |
| `.../policy.{h,cpp}` | verdicts, rollout bucket, path allowlist, RC path, install choice |
| `.../manifest.{h,cpp}` | strict JSON manifest parse into an owning struct |
| `.../hal.h` | `IOtaStore`, `IOtaSlots`, `IOtaHttp` |
| `.../state.{h,cpp}` | floor, pending build, blacklist, ETag over `IOtaStore` |
| `.../updater.{h,cpp}` | `onBoot()`, `check()`, `confirmBoot()` |
| `lib/crash_breadcrumb/src/crash_breadcrumb/crash_breadcrumb.{h,cpp}` | RTC label store and crash-report JSON |
| `lib/net_schedule/...` | new `Action::kCheckOta` |
| `src/hardware/ota_platform.cpp` + `include/hardware/ota_platform.h` | adapters over `Preferences`, `esp_ota`, `Update`, `HTTPClient` |
| `src/services/ota_service.cpp` + `include/services/ota_service.h` | owns the `Updater`, confirm tick, `verifyRollbackLater()` |
| `src/services/crash_log.cpp` + `include/services/crash_log.h` | RTC breadcrumb instance, `/api/crash-log` handler |
| `scripts/ota_release.py`, `scripts/make_ota_test_vector.py` | keygen, sign, promote, publish; deterministic test vector |
| `test/support/ota_fakes.h`, `ota_vectors.h`, `tweetnacl_verify.h` | fakes, generated vector, header-only test Ed25519 verify |

---

### Task 1: Two-slot build

**Files:**
- Modify: `partitions/plane_radar.csv`, `platformio.ini`, `README.md`
- Check: `scripts/merge_firmware.py`, `scripts/merge-firmware.sh`, `.github/workflows/release.yml`

**Interfaces:**
- Produces: a build whose `firmware.bin` fits `app0`/`app1` of `0x1F0000` each; `rweather/Crypto@^0.4.0` available to `src/`; `-std=gnu++17` actually in effect for the `supermini` env.

- [ ] **Step 1:** Replace the table with `nvs 0x9000/0x5000`, `otadata 0xe000/0x2000`, `app0 ota_0 0x10000/0x1F0000`, `app1 ota_1 0x200000/0x1F0000`, `coredump 0x3F0000/0x10000` (no `spiffs`; nothing uses it). Header comment says two OTA slots.
- [ ] **Step 2:** In `[env:supermini]` add `build_unflags = -std=gnu++11` and the Crypto `lib_deps` entry. Build and confirm no new warnings beyond the framework's `esp32-hal-uart.c` one: `pio run -e supermini` exits 0.
- [ ] **Step 3:** Read `merge_firmware.py` and the release workflow; confirm the merged image places the table at `0x8000` and the app at `0x10000`, and that nothing hard-codes the old `spiffs`. Fix only what breaks.
- [ ] **Step 4:** README: one paragraph stating the first flash of a board must be a full USB flash because the partition table changed.
- [ ] **Step 5:** Verify `pio run -e supermini` output reports Flash under 2,031,616 B; commit `build: two OTA slots and C++17`.

### Task 2: Signature verification

**Files:**
- Create: `lib/ota_core/library.json`, `lib/ota_core/src/ota_core/{types.h,keys.h,signature.h,signature.cpp}`, `scripts/make_ota_test_vector.py`, `test/support/{ota_vectors.h,tweetnacl_verify.h}`, `test/test_ota_signature/test_main.cpp`

**Interfaces:**
- Produces (namespace `ota_core`):
  - `using Ed25519VerifyFn = bool (*)(const uint8_t* sig, const uint8_t* pub, const uint8_t* msg, size_t len);`
  - `struct KeyList { const uint8_t* const* keys; size_t count; };`
  - `enum class Channel : uint8_t { kProd = 1, kRc = 2 };` `constexpr uint8_t channelBit(Channel)`.
  - `constexpr size_t kKeyBytes = 32, kSigBytes = 64, kShaBytes = 32, kDeviceIdBytes = 32, kPayloadBytes = 91;`
  - `bool decodeSignatureB64(const char* b64, uint8_t out[kSigBytes]);` (88 chars, `==` padding, canonical, null refused)
  - `void computeDeviceId(const uint8_t mac6[6], uint8_t out[kDeviceIdBytes]);` `void deviceHashHex(const uint8_t id[kDeviceIdBytes], char out[33]);`
  - `size_t buildPayload(const uint8_t sha[32], uint32_t build, uint32_t minBuild, Channel, const uint8_t deviceId[32], uint8_t out[kPayloadBytes]);`
  - `enum class SigError : uint8_t { kNone, kMissingSignature, kBadSignatureEncoding, kUnknownChannel, kChannelNotAccepted, kBadHash, kBadBuild, kBadMinBuild, kMinBuildAboveBuild, kBadDeviceId, kBadSignature };` `const char* sigErrorName(SigError)` (snake_case, `"none"` for `kNone`).
  - `struct U32Field { bool valid; uint32_t value; }; struct ManifestFields { U32Field build, minBuild; const char* sha256Hex; const char* channel; const char* signature; }; struct VerifiedOffer { uint8_t sha256[32]; uint32_t build; uint32_t minBuild; Channel channel; };`
  - `SigError verifyManifest(const ManifestFields&, uint8_t acceptedChannelMask, const uint8_t ownDeviceId[32], const KeyList&, Ed25519VerifyFn, VerifiedOffer& out);` checks in enum order; `ownDeviceId` all-zero is `kBadDeviceId`; prod signs over zeros, rc over `ownDeviceId`; disabled keys skipped; `out` written only on `kNone`.

- [ ] **Step 1:** Write `make_ota_test_vector.py` using `cryptography` Ed25519 with a fixed 32-byte seed; it emits `test/support/ota_vectors.h` with the public key, a prod signature and an rc signature for a fixed sha/build/min_build/device id, and a wrong-key signature. Run it and commit the generated header (test key only).
- [ ] **Step 2:** Copy the verify-only subset of TweetNaCl (public domain, from `~/Documents/Arduino/metarmap/tests/vendor/ed25519/tweetnacl_verify.cpp`) into a header-only `test/support/tweetnacl_verify.h` exposing `inline bool tweetnaclVerify(sig, pub, msg, len)`.
- [ ] **Step 3:** Write failing tests: `PayloadLayoutIsExact` (91 bytes, prefix, big-endian build/min_build, channel byte, zero id for prod and own id for rc), `GoldenProdSignatureVerifies` and `GoldenRcSignatureVerifies` (real `tweetnaclVerify`), `TamperedBuildFailsBadSignature`, `WrongKeyFailsBadSignature`, `ProdSignatureDoesNotVerifyAsRc` and the reverse, `AllZeroOwnDeviceIdIsBadDeviceId`, `DisabledKeySkippedSecondKeyAccepted`, one test per other `SigError` value (missing/encoding/channel/hash/build/min_build/min_build_above_build), `SigErrorNamesAreSnakeCase`, `OutUntouchedOnError`, `DecodeRejectsWrongLengthPaddingNonCanonicalAndNull`, `DeviceIdIsSha256OfPrefixedMac` and `DeviceHashHexIsFirst16BytesLowercase`.
- [ ] **Step 4:** Run `pio test -e native -f test_ota_signature`; expect compile failure (no library). Implement `types.h`, `keys.h`, `signature.*`; `computeDeviceId` uses Task 3's `Sha256`, so implement Task 3 first or stub then replace (do Task 3 before this step if executing in order).
- [ ] **Step 5:** `pio test -e native -f test_ota_signature` passes; `scripts/coverage.sh` shows 100% for the new files; commit `feat: OTA manifest signature verification`.

### Task 3: SHA-256

**Files:**
- Create: `lib/ota_core/src/ota_core/{sha256.h,sha256.cpp}`, `test/test_ota_sha256/test_main.cpp`

**Interfaces:**
- Produces: `class Sha256 { public: void start(); void update(const uint8_t* data, size_t len); void finish(uint8_t out[32]); };` Host and target share the portable implementation (`#if defined(ARDUINO)` selects mbedtls inside the `.cpp`; the host build compiles only the portable path).

- [ ] **Step 1:** Failing tests with known answers: `EmptyInput`, `Abc`, `TwoBlockMessage` ("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"), `StreamingSplitsMatchOneShot` (every split point of a 130-byte buffer), `ReuseAfterFinishNeedsStart`.
- [ ] **Step 2:** Run `pio test -e native -f test_ota_sha256`; expect fail. Implement.
- [ ] **Step 3:** Pass and 100% coverage; commit `feat: streaming SHA-256`.

### Task 4: Policy

**Files:**
- Create: `lib/ota_core/src/ota_core/{policy.h,policy.cpp}`, `test/test_ota_policy/test_main.cpp`

**Interfaces:**
- Produces:
  - `constexpr uint32_t kMaxSaneBuild = 999999;`
  - `enum class Verdict : uint8_t { kAccept, kSuspiciousBuild, kBlacklisted, kBelowMinBuild, kServerRollback, kAlreadyCurrent, kNotInRollout };` `const char* verdictName(Verdict)` (snake_case).
  - `struct Offer { uint32_t build; uint32_t minBuild; }; struct LocalState { uint32_t build; uint32_t floor; bool blacklisted; bool inRollout; };`
  - `Verdict evaluateOffer(const Offer&, const LocalState&);` rule order: suspicious, blacklisted, below `max(offer.minBuild, floor)`, server rollback (`< local.build`), already current (`==`), not in rollout, else accept.
  - `bool inRolloutGroup(const uint8_t deviceId[32], uint8_t rollout);` bucket `((id[0] << 8) | id[1]) % 100 < rollout`; 0 never, >=100 always.
  - `bool isAllowedPath(const char* path);` begins with one `/`, length 2-96, chars `[A-Za-z0-9._/-]` only, no `//`, no `..` component.
  - `bool formatRcPath(const char* basePath, const char hashHex[33], char* out, size_t cap);` yields `<basePath>/rc/<hashHex>.json`, false if it does not fit or `hashHex` is not exactly 32 lowercase hex characters.
  - `bool isListed(const char* const* entries, size_t count, const char ownHashHex[33]);`
  - `enum class InstallChoice : uint8_t { kNone, kProduction, kCandidate };` `InstallChoice chooseInstall(bool prodAccepted, uint32_t prodBuild, bool rcAccepted, uint32_t rcBuild);` candidate wins only when accepted and (prod not accepted or `rcBuild > prodBuild`).

- [ ] **Step 1:** Failing tests: one per verdict plus rule-order cases (`SuspiciousBeatsBlacklisted`, `BlacklistedBeatsBelowMinBuild`, offer `min_build` and stored floor each raise the effective floor and neither lowers the other), `RolloutZeroAndHundred`, `RolloutBucketIsStableAndSpreads` (1000 synthetic ids land within 20% of the expected share at 50), `PathAllowlistRejects` for `"firmware.bin"`, `"//evil/x"`, `"/a/../b"`, `"/a?x=1"`, `"/a#b"`, `"https://h/x"`, `"/a b"`, `"/"`, a 97-char path, and `"/\xff"`; accepts `"/planeradar/firmware-8.bin"`, `FormatRcPathRejectsBadHashAndSmallBuffer`, `IsListedExactMatchOnly`, and `ChooseInstall` truth table including `RcOlderThanProdDoesNotDowngrade` and `PromotedRcBuildEqualsProdIsNotInstalled`.
- [ ] **Step 2:** Run (`-f test_ota_policy`), expect fail; implement; pass; 100% coverage; commit `feat: OTA policy`.

### Task 5: Manifest parsing

**Files:**
- Create: `lib/ota_core/src/ota_core/{manifest.h,manifest.cpp}`, `test/test_ota_manifest/test_main.cpp`

**Interfaces:**
- Consumes: `U32Field`, `ManifestFields` (Task 2).
- Produces:
  - `constexpr size_t kMaxManifestBytes = 1024, kMaxCandidates = 8, kPathMax = 96;`
  - `struct Manifest { U32Field build, minBuild; char sha256[65]; char channel[8]; char signature[89]; char firmware[kPathMax + 1]; uint8_t rollout; char candidates[kMaxCandidates][33]; size_t candidateCount; ManifestFields fields() const; };`
  - `enum class ParseError : uint8_t { kNone, kTooLarge, kNotJson, kMissingField, kBadField };`
  - `ParseError parseManifest(const char* body, size_t len, Manifest& out);` required: `build`, `min_build`, `sha256`, `channel`, `signature`, `firmware`; optional `rollout` (integer 0-100, default 100) and `candidates` (array of up to 8 strings of exactly 32 lowercase hex, else `kBadField`). `build`/`min_build` must be JSON unsigned integers that fit 32 bits (a string is `kBadField`, reported as `valid=false` so the verifier names it). Over-long strings are `kBadField`, never truncated.

- [ ] **Step 1:** Failing tests: `ParsesFullManifest`, `RolloutDefaultsTo100`, `RolloutOver100IsBadField`, `NonObjectIsNotJson`, `OverCapIsTooLarge` (1025 bytes), `MissingEachRequiredField` (parameterised), `BuildAsStringIsInvalidNotMissing`, `HugeBuildIsInvalid`, `TooManyCandidatesIsBadField`, `CandidateWithUppercaseIsBadField`, `LongSignatureIsBadField`, `LongFirmwarePathIsBadField`, `UnknownKeysIgnored`, `DuplicateKeysLastWinsOrBadField` (pin whichever ArduinoJson 7 does and assert it).
- [ ] **Step 2:** Run, expect fail; implement with ArduinoJson `JsonDocument` and `deserializeJson`; pass; coverage; commit `feat: OTA manifest parser`.

### Task 6: Persisted state

**Files:**
- Create: `lib/ota_core/src/ota_core/{hal.h,state.h,state.cpp}`, `test/support/ota_fakes.h`, `test/test_ota_state/test_main.cpp`

**Interfaces:**
- Produces (`hal.h`):
  - `class IOtaStore { public: virtual ~IOtaStore() = default; virtual bool ok() const = 0; virtual uint32_t getUInt(const char* key, uint32_t def) = 0; virtual bool putUInt(const char* key, uint32_t v) = 0; virtual size_t getBytes(const char* key, void* buf, size_t cap) = 0; virtual bool putBytes(const char* key, const void* buf, size_t len) = 0; virtual bool erase(const char* key) = 0; };`
  - `enum class SlotState : uint8_t { kUndefined, kNew, kPendingVerify, kValid, kInvalid, kAborted };`
  - `class IOtaSlots { public: virtual ~IOtaSlots() = default; virtual SlotState runningState() = 0; virtual bool hasInvalidSlot() = 0; virtual bool markRunningValid() = 0; virtual bool beginUpdate(size_t size) = 0; virtual size_t writeUpdate(const uint8_t* data, size_t len) = 0; virtual bool endUpdate() = 0; virtual void abortUpdate() = 0; };`
  - `struct ManifestResponse { int status; char etag[65]; size_t bodyLen; char body[kMaxManifestBytes + 1]; bool tooLarge; };`
  - `class IOtaHttp { public: virtual ~IOtaHttp() = default; virtual bool fetchManifest(const char* path, const char* ifNoneMatch, ManifestResponse& out) = 0; virtual int64_t openImage(const char* path) = 0; virtual int readImage(uint8_t* buf, size_t cap) = 0; virtual void closeImage() = 0; };` `openImage` returns the content length (>= 0) or a negative HTTP status or -1 for transport failure; `readImage` returns bytes read, 0 at end of stream, -1 on timeout or error.
- Produces (`state.h`): `class OtaState { public: explicit OtaState(IOtaStore&); bool ok() const; uint32_t floor() const; bool raiseFloor(uint32_t build); uint32_t pending() const; bool setPending(uint32_t build); bool clearPending(); bool isBlacklisted(uint32_t build) const; bool blacklist(uint32_t build); size_t etag(char* out, size_t cap) const; bool setEtag(const char* etag); bool clearEtag(); };` `raiseFloor` never lowers; the blacklist holds 8 builds, evicting the oldest, idempotent per build.
- `ota_fakes.h` provides `FakeStore` (in-memory map, `okValue` flag), `FakeSlots`, `FakeHttp`, each with `std::function` hooks defaulting to benign behaviour.

- [ ] **Step 1:** Failing tests: `FloorOnlyRises`, `PendingSetClear`, `BlacklistRecordsAndQueries`, `BlacklistIsIdempotent`, `NinthBuildEvictsOldest` (Review Focus 4: after 9 adds the first is gone, the other 8 remain, order preserved), `EtagRoundTripAndTruncationRefused` (65+ chars refused), `ClearEtag`, `StoreNotOkReportsNotOkAndWritesFail` (Review Focus 3: every mutator returns false, nothing written), `CorruptBlacklistBlobReadsAsEmpty`.
- [ ] **Step 2:** Run, expect fail; implement (blacklist as one blob of count + up to 8 `uint32_t`); pass; coverage; commit `feat: persisted OTA state`.

### Task 7: Boot handling

**Files:**
- Create: `lib/ota_core/src/ota_core/{updater.h,updater.cpp}` (boot half), `test/test_ota_updater/test_boot.cpp`

**Interfaces:**
- Consumes: `IOtaStore`, `IOtaSlots`, `IOtaHttp`, `OtaState`.
- Produces:
  - `struct Config { const char* basePath; uint8_t deviceId[32]; uint32_t runningBuild; KeyList keys; Ed25519VerifyFn verify; };`
  - `struct BootInfo { bool pendingVerify; bool rolledBack; uint32_t rolledBackBuild; };`
  - `class Updater { public: Updater(const Config&, IOtaStore&, IOtaSlots&, IOtaHttp&); BootInfo onBoot(); bool confirmBoot(); CheckResult check(); };` (`CheckResult` is added in Task 8.)
  - `onBoot()`: when stored pending build differs from `runningBuild`: if `hasInvalidSlot()` blacklist it and report `rolledBack`, else (stale pending from a USB flash) just clear; always clear in both cases. `pendingVerify` is `runningState() == kPendingVerify`.
  - `confirmBoot()`: only when `kPendingVerify`: `markRunningValid()`, then `raiseFloor(runningBuild)`, `clearPending()`; returns false when marking fails or the store is not ok; when the image is already valid it returns true and writes nothing.

- [ ] **Step 1:** Failing tests: `CleanBootReportsNothing`, `PendingForRunningBuildIsPendingVerify`, `PendingForOtherBuildWithInvalidSlotRollsBackAndBlacklists`, `PendingForOtherBuildWithoutInvalidSlotIsStaleAndCleared` (floor and blacklist untouched), `ConfirmMarksValidThenRaisesFloorAndClearsPending` (assert call order), `ConfirmWhenAlreadyValidWritesNothing`, `ConfirmFailureLeavesFloorAndPendingUnchanged`, `FloorNeverRaisedByManifestOrCheck`.
- [ ] **Step 2:** Run, expect fail; implement; pass; coverage; commit `feat: OTA boot handling and confirm`.

### Task 8: Production check and install

**Files:**
- Modify: `lib/ota_core/src/ota_core/updater.{h,cpp}`; Test: `test/test_ota_updater/test_check.cpp`

**Interfaces:**
- Consumes: Tasks 2-7.
- Produces:
  - `enum class Outcome : uint8_t { kNotModified, kNoUpdate, kRejected, kFailed, kInstalled };`
  - `struct CheckResult { Outcome outcome; const char* reason; uint32_t build; bool candidate; };` `reason` is one of `"not_modified"`, a `verdictName`, a `sigErrorName`, `"fetch_failed"`, `"malformed"`, `"bad_path"`, `"store_unavailable"`, `"begin_failed"`, `"write_failed"`, `"download_incomplete"`, `"hash_mismatch"`, `"end_failed"`, `"installed"`.
  - `check()` order: store not ok -> `kFailed store_unavailable`; fetch prod manifest at `<basePath>/manifest.json` with the stored ETag (transport failure or any status other than 200/304 -> `kFailed fetch_failed`; 304 -> `kNotModified`); parse (`kRejected malformed`); `verifyManifest` with the prod mask (`kRejected <sigErrorName>`); `isAllowedPath(firmware)` (`kRejected bad_path`); `evaluateOffer` with floor, blacklist and `inRolloutGroup(deviceId, rollout)`. `kAlreadyCurrent` and `kNotInRollout` are `kNoUpdate`; every other non-accept verdict is `kRejected`. Install: `beginUpdate(len)`, stream while hashing 2 KB at a time with a `readImage` that returns -1 or 0 early meaning `download_incomplete`, compare the hash, then `setPending(build)` **before** `endUpdate()` (clear pending if `endUpdate` fails), return `kInstalled`.
  - ETag rule: store the response ETag only when the signature verified and the outcome is `kNoUpdate`, `kRejected` (verified-signature verdicts) or `kInstalled`; never on `kFailed` and never on unverified or malformed manifests (Review Focus 1).

- [ ] **Step 1:** Failing tests mirroring the spike's 12 scenarios: `ControlInstalls` (pending written before `endUpdate`, `endUpdate` called once, build reported), `BadSignature`, `WrongKey`, `DowngradeIsRejectedServerRollback`, `SameBuildIsNoUpdateAlreadyCurrent`, `CorruptImageIsHashMismatchAndAborts`, `TruncatedStreamIsDownloadIncompleteAndAborts`, `StallReadReturnsMinusOneIsDownloadIncomplete`, `WriteFailureAborts`, `ImageLargerThanSlotIsBeginFailed` (nothing read), `BadJsonIsMalformed`, `Http404IsFetchFailed`; plus `NotModifiedSkipsEverything`, `IfNoneMatchSentFromStore`, `EtagStoredAfterNoUpdate`, `EtagNotStoredAfterFailedDownload` and `EtagNotStoredAfterTruncatedDownload` (Review Focus 1), `EtagNotStoredForBadSignature`, `BadPathNeverOpensImage`, `BlacklistedBuildRejected`, `BelowFloorRejected`, `NotInRolloutIsNoUpdate`, `StoreNotOkFailsClosedWithoutHttp` (Review Focus 3), `EndUpdateFailureClearsPending`, `AbortNeverActivates` (no `endUpdate` on any failure path).
- [ ] **Step 2:** Run, expect fail; implement; pass; coverage 100%; commit `feat: OTA production check and install`.

### Task 9: Release candidates

**Files:**
- Modify: `lib/ota_core/src/ota_core/updater.cpp`; Test: `test/test_ota_updater/test_candidate.cpp`

**Interfaces:**
- Consumes: `isListed`, `formatRcPath`, `chooseInstall`, `deviceHashHex`, `verifyManifest` with `channelBit(Channel::kRc)`.
- Produces: `check()` additionally, when the prod manifest verified and lists this board's hash, fetches `formatRcPath(basePath, ownHash)` (no ETag); `404` means no candidate; a malformed, badly signed or non-accepted candidate never blocks a production install; the candidate is evaluated at rollout 100 against the same floor and blacklist; `chooseInstall` picks the image; a candidate install reports `candidate = true`.

- [ ] **Step 1:** Failing tests: `ListedBoardFetchesOwnRcManifest` (path exact), `UnlistedBoardNeverFetchesRc`, `Rc404FallsBackToProd`, `RcWithProdSignatureRejected` and `ProdWithRcSignatureRejected` (channel binding), `RcSignedForAnotherBoardRejected`, `RcNewerThanProdInstalledAsCandidate`, `RcOlderThanProdIgnored`, `PromotedBuildIsAlreadyCurrentOnRcBoard` (install RC build 9, rerun `check()` with the prod manifest for the same build and sha: `kNoUpdate already_current`), `BadRcDoesNotBlockProd`, `BlacklistedRcRejected`, `RcDownloadFailureLeavesProdUntouchedAndNoEtag`.
- [ ] **Step 2:** Run, expect fail; implement; pass; coverage 100%; commit `feat: OTA release candidates`.

### Task 10: Crash breadcrumbs

**Files:**
- Create: `lib/crash_breadcrumb/library.json`, `lib/crash_breadcrumb/src/crash_breadcrumb/crash_breadcrumb.{h,cpp}`, `test/test_crash_breadcrumb/test_main.cpp`

**Interfaces:**
- Produces (namespace `crash_breadcrumb`):
  - `constexpr uint32_t kMagic = 0x43524142; constexpr size_t kLabelMax = 24; constexpr size_t kMaxBlacklist = 8;`
  - `struct Breadcrumb { uint32_t magic; uint32_t uptimeMs; char label[kLabelMax]; };`
  - `void set(Breadcrumb& b, const char* label, uint32_t uptimeMs);` truncates, always NUL-terminates, sets the magic.
  - `size_t copyLabel(const Breadcrumb& b, char* out, size_t cap);` length copied; empty when the magic is wrong or any byte before the NUL is not printable ASCII (RTC memory is garbage after a power-on).
  - `struct Report { const char* resetReason; const char* checkpoint; uint32_t checkpointUptimeMs; uint32_t runningBuild; uint32_t pendingBuild; uint32_t blacklist[kMaxBlacklist]; size_t blacklistCount; uint32_t heapFree; uint32_t heapLowest; const char* slot; };`
  - `size_t formatReportJson(const Report&, char* out, size_t cap);` writes `{"reset":"..","checkpoint":"..","checkpoint_ms":N,"build":N,"pending":N,"blacklist":[..],"heap_free":N,"heap_min":N,"slot":".."}` and returns its length, or 0 when `cap` is too small; strings are JSON-escaped.

- [ ] **Step 1:** Failing tests: `SetTruncatesAndTerminates`, `CopyLabelRoundTrip`, `GarbageMagicReadsEmpty`, `NonPrintableReadsEmpty`, `CopyLabelRespectsSmallCap`, `ReportJsonExactForKnownInput`, `ReportJsonEscapesQuoteAndBackslash`, `ReportJsonTooSmallReturnsZero`, `EmptyBlacklistIsEmptyArray`.
- [ ] **Step 2:** Run, expect fail; implement; pass; coverage; commit `feat: crash breadcrumbs`.

### Task 11: Scheduler action for OTA

**Files:**
- Modify: `lib/net_schedule/src/net_schedule/scheduler.{h,cpp}`, `include/config.h`; Test: `test/test_net_schedule/test_main.cpp`

**Interfaces:**
- Produces: `Action::kCheckOta`; `Config` gains `unsigned long ota_first_ms, ota_interval_ms, ota_retry_ms`; `Scheduler::setOtaJitterMs(unsigned long)`, `otaStarted()`, `otaFinished(unsigned long now_ms, bool ok)`. `next()` returns `kCheckOta` ahead of ADS-B and feeds once due: first at `ota_first_ms`, then `ota_interval_ms + jitter` after a success, `ota_retry_ms` after a failure; all comparisons survive `millis()` wrapping. `config.h` adds `kOtaFirstCheckMs = 5 min`, `kOtaIntervalMs = 6 h`, `kOtaJitterMaxMs = 6 h`, `kOtaRetryMs = 30 min`. Existing `Config` initialisers in tests and `net_worker.cpp` are updated.

- [ ] **Step 1:** Failing tests: `OtaNotDueBeforeFirstDelay`, `OtaDueAtFirstDelay`, `OtaBeatsAdsbAndFeeds`, `OtaSuccessWaitsIntervalPlusJitter`, `OtaFailureRetriesAfterRetryGap`, `OtaSurvivesMillisWrap`, `JitterZeroMeansExactInterval`, and every existing scheduler test still passes.
- [ ] **Step 2:** Run `pio test -e native -f test_net_schedule`, expect fail; implement; pass; coverage; commit `feat: OTA check in the network scheduler`.

### Task 12: Firmware adapters and wiring

**Files:**
- Create: `include/hardware/ota_platform.h`, `src/hardware/ota_platform.cpp`, `include/services/ota_service.h`, `src/services/ota_service.cpp`, `include/services/crash_log.h`, `src/services/crash_log.cpp`
- Modify: `src/services/net_worker.cpp`, `src/services/wifi_setup.cpp` (`registerPortalRoutes`), `src/main.cpp`, `include/config.h`, `lib/ota_core/src/ota_core/keys.h`

**Interfaces:**
- Consumes: everything above.
- Produces:
  - `ota_platform.h`: `class PrefsStore : public ota_core::IOtaStore` (namespace `"otacore"`, `ok()` is the result of `Preferences::begin`), `class EspSlots : public ota_core::IOtaSlots` (`esp_ota_get_state_partition`, `esp_ota_get_last_invalid_partition`, `esp_ota_mark_app_valid_cancel_rollback`, `Update`), `class TlsHttp : public ota_core::IOtaHttp` (`WiFiClientSecure` with `setCACertBundle(_binary_data_x509_crt_bundle_bin_start)`, host `config::kOtaHost`, 15 s idle timeout, never follows redirects), `void factoryMac(uint8_t out[6])` (`esp_efuse_mac_get_default`).
  - `ota_service.h` (namespace `services::ota`): `void init();` builds the `Updater` (running build from `config::kFirmwareBuild`, id from `computeDeviceId(factoryMac)`), calls `onBoot()`, and logs the result; `ota_core::CheckResult checkNow();` closes nothing itself (the worker does); `void confirmTick();` called from `loop()`: when `kPendingVerify`, WiFi is connected and one ADS-B poll has succeeded since boot, call `confirmBoot()`; if still pending 10 minutes after boot, `esp_restart()` (bootloader rolls back); while pending, `enableLoopWDT()` so a hung `loop()` resets. `extern "C" bool verifyRollbackLater() { return true; }` lives here.
  - `crash_log.h` (namespace `services::crash_log`): `void checkpoint(const char* label);` writes an `RTC_NOINIT_ATTR` `Breadcrumb` (no flash writes); `size_t reportJson(char* out, size_t cap);`; the route `GET /api/crash-log` is registered in `registerPortalRoutes()`.
  - `net_worker.cpp`: `case kCheckOta:` calls `crash_log::checkpoint("ota_check")`, `adsb::closeConnection()`, `scheduler.otaStarted()`, `services::ota::checkNow()`, `scheduler.otaFinished(millis(), result.outcome != kFailed)`, draws a fresh `esp_random() % config::kOtaJitterMaxMs` into `setOtaJitterMs`, restarts the board (`ESP.restart()` after a 500 ms delay) only on `kInstalled`; the other job cases set their own checkpoints.
  - `config::kFirmwareBuild` (uint32), `kOtaHost = "metarly.app"`, `kOtaBasePath = "/planeradar"`.

- [ ] **Step 1:** Implement the adapters and wiring; keep `setup()` order: `crash_log::checkpoint("setup")` first, `services::ota::init()` right after the Serial banner. Do not add display UI.
- [ ] **Step 2:** `pio run -e supermini` builds warning-free and Flash stays under 2,031,616 B; record the size in the commit message.
- [ ] **Step 3:** `pio test -e native` and `scripts/coverage.sh` still pass (the firmware files are outside `lib/`).
- [ ] **Step 4:** Commit `feat: wire OTA updater and crash log into the firmware`.

### Task 13: On-device acceptance

**Files:**
- Modify: `README.md`; Create: `docs/ota-acceptance.md` (procedure and results)

**Interfaces:**
- Consumes: Task 12 firmware on the C3, a local HTTPS server using the spike's throwaway CA and `scripts/ota_release.py` (Task 14) with a throwaway test key, and a temporary `config::kOtaHost` override via build flag.

- [ ] **Step 1:** Flash over USB (hold BOOT, tap RESET, release BOOT fully, tap RESET again). Confirm `boot` logs `onBoot` and the radar draws.
- [ ] **Step 2:** Good update with the radar running: serve build N+1; record heap lowest-ever and net-task stack headroom (Review Focus 5). Pass if the update installs, confirms, and headroom is at least 1 KB; if not, raise the `net` task stack or pause rendering during the check and re-measure.
- [ ] **Step 3:** Bad update (image that panics at boot): the board rolls back, `/api/crash-log` shows the blacklisted build, the next check says `blacklisted`.
- [ ] **Step 4:** Hang update (image that spins in `loop()`): loop watchdog resets it and it rolls back. Net-task hang (image that blocks the `net` task): the 10-minute deadline restarts it and it rolls back.
- [ ] **Step 5:** Release candidate: list the board in the prod manifest, serve an rc manifest, confirm the board installs it, then promote (same sha, prod signature) and confirm the board reports `already_current`.
- [ ] **Step 6:** Real host: after David confirms hosting, run one manifest fetch against the real `metarly.app` chain to prove the Mozilla bundle validates it. Record results in `docs/ota-acceptance.md`; commit `docs: OTA acceptance results`.

### Task 14: Release tooling

**Files:**
- Create: `scripts/ota_release.py`, `scripts/tests/test_ota_release.py`

**Interfaces:**
- Produces: `ota_release.py keygen --out PATH` (creates the private key with `O_EXCL` and mode `0600`, prints the public key as a C array and refuses to overwrite); `sign --key PATH --firmware FILE --build N --min-build M --channel prod|rc [--device-hash HEX32] [--rollout 0-100] [--candidate HEX32 ...] --out manifest.json`; `promote --key PATH --rc-manifest FILE --firmware FILE --out manifest.json` (re-signs the same sha as prod; refuses when the firmware's sha256 differs from the rc manifest's); `publish --dest SSH_TARGET --dir DIR --yes` (rsync of manifests and images; without `--yes` it prints what it would do and exits 0). The signed payload layout matches Global Constraints byte for byte.

- [ ] **Step 1:** Failing tests (`unittest`, no network): `KeygenRefusesToOverwriteAndIsMode0600`, `SignedPayloadMatchesDeclaredLayout` (rebuild the 91 bytes independently and verify the signature with `cryptography`), `RcManifestBindsDeviceHash`, `PromoteKeepsShaAndChangesChannelAndSignature`, `PromoteRefusesDifferentFirmware`, `CandidateListRejectsMoreThanEightAndBadHex`, `PublishWithoutYesDoesNotRunRsync`, `ManifestStaysUnderCap` (1024 bytes with 8 candidates).
- [ ] **Step 2:** `python -m unittest discover -s scripts/tests -v` fails, implement, passes; commit `feat: OTA release tooling`.
- [ ] **Step 3:** Cross-check: the vector from Task 2 verifies with `sign` output built from the same fixed key (one extra test `ReleaseToolMatchesCoreVector`).

---

## Self-Review

- **Spec coverage:** staged rollout (Tasks 4, 8), crash breadcrumbs (10, 12), ETag (6, 8), persisted blacklist (6, 7), release candidates (4, 9, 13, 14), no dev tickets (absent by design), two-slot build (1), automatic rollback and hang handling (7, 12, 13), scheduling (11), release tooling (14). The earlier "Needs replacing" items map to Tasks 12 and 14; host tests are GoogleTest in Tasks 2-10.
- **Type consistency:** `ManifestFields` and `VerifiedOffer` (Task 2) are consumed unchanged in Tasks 5, 8, 9; `Offer`, `LocalState`, `Verdict` (Task 4) are used by Task 8; HAL types (Task 6) are used by Tasks 7-9 and implemented in Task 12; `CheckResult` is introduced in Task 8 and referenced in Task 7's class declaration (the declaration is completed in Task 8).
- **Order dependency:** Task 2 needs Task 3's `Sha256` for `computeDeviceId`; execute 3 before 2. Task 13 needs Task 14's `sign` and `promote`; execute 14 before 13.
- **Open decisions for David:** hosting host/path (before Task 12 sets constants), and whether to commit `ota-spike` locally as a reference.
