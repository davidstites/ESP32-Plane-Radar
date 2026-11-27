# OTA on-device acceptance

Status: steps 1-6 and the recovery button have been run on hardware (2026-11-25), and the Wi-Fi outage cases (issue #22) the power cuts (issue #23) and the crash before `setup()` (issue #24) on 2026-11-26, and the power cut inside the confirmation window (issue #35) on 2026-11-27; the items under "Still open" have not. Results are in the table below.

Setup: a C3 on USB, the firmware from this branch built with a throwaway release key in `keys.h` (never the real one), a local HTTPS server using a throwaway CA, and `-DPLANE_RADAR_BUILD=N` / a temporary `config::kOtaHost` pointing at the server. `scripts/ota_release.py` signs the manifests. `scripts/ota_rig.py` automates all of it: `init` makes the throwaway keys and certificates, `build N` makes a throwaway image (HEAD plus uncommitted changes, with an optional `--window` for the confirmation time), `upload N` flashes it, `serve N` signs and serves it, and `capture` logs serial across USB resets. The cable pull and the AP power-down stay manual. Spike helpers are in `git stash` (`ota-spike`), reference only.

Flashing the C3: hold BOOT, tap RESET, release BOOT fully, tap RESET again.

| # | Step | Pass when | Result |
|---|------|-----------|--------|
| 1 | Flash over USB | log shows `ota: build N, device <hash>, slot app0, store ok`; radar draws | **pass** 2026-11-25 |
| 2 | Good update (build N+1) with the radar running | installs, reboots into app1, logs `new build confirmed`; lowest-ever heap and net-task stack headroom recorded; headroom >= 1 KB (else raise the `net` task stack or pause rendering during the check) | **pass** (local HTTPS, HTTP/1.0 manifest 200). Confirmed 59 s after boot. Stack headroom 2,980 B; lowest-ever free heap 15-20 KB during a check; largest block 33-49 KB before it. See the heap note below |
| 3 | Bad update (image that panics at boot) | bootloader rolls back; `/api/crash-log` lists the build under `blacklist`; next check reports `blacklisted` | **pass** (abort() 15 s after boot; reset `panic`, `blacklist:[3]`, next check `blacklisted`) |
| 4a | Hang in `loop()` | loop watchdog resets; rolls back | **pass** (`task_wdt: loopTask` at ~19 s, abort, rolled back, blacklisted) |
| 4b | Hang that blocks the `net` task | no confirmation within 10 min; board restarts; rolls back | superseded: confirmation is local only, so a stuck `net` task deliberately never rolls back |
| 5 | Release candidate | list the board in the prod manifest, serve `rc/<hash>.json`: board installs it (`candidate`); `promote` the same bytes, board reports `already_current` | **pass** (installed build 5 as candidate, confirmed; after `promote`: `already_current`) |
| 6 | Real host | one manifest fetch against `metarly.app` validates with the Mozilla bundle (needs hosting confirmed) | **pass** 2026-11-25 (real key, real host; build 6 installed build 7 at +5 min, confirmed at +59 s, next check `already_current`). Lowest-ever heap 10 KB during the download. 304 confirmed 2026-11-25 (see below) |

Power cuts are recorded under "Run on 2026-11-26: power cuts", the crash before `setup()` under "Run on 2026-11-26: crash before setup()".

## Added after the final review (unverified on hardware)

- The manifest GET now uses HTTP/1.0 (`useHTTP10`) so a chunked response cannot appear; confirm a 200 and a 304 against the real host still parse.
- Confirmation is local only: a new build confirms after 60 s of running (`ota: new build confirmed`), with no network involved. The Wi-Fi outage run is recorded under "Run on 2026-11-26" below. A crash or hang inside that minute (e.g. a deliberate `abort()` in `loop()`) must roll back and be blacklisted.
- Base path is `/ota/planeradar` (manifest, `firmware-N.bin`, `rc/<hash>.json`).
- Recovery button: after an update, power-cycle and hold BOOT (pressed just after power-up, never through reset) for 3 s; expect `ota: recovery button held` and a restart into the previous build, which must not reinstall the blacklisted one. With only one image installed expect `no previous firmware to return to` and a normal boot. A normal boot with no button must not be delayed beyond about 2.5 s.

## Run on 2026-11-25 (throwaway key, local HTTPS)

- Recovery button: tap RESET then hold BOOT ~4 s. Board returned to the previous build (build 2 in app0), blacklist `[3,4,5]`, pending 0, reset reason `software`. The next check reported `blacklisted (build 5)` and did not reinstall it.
- HTTP/1.0 manifest: 200 verified against the local server (it sends no ETag). The 304 is covered by the real-host entry below.
- **Heap note.** 3 of about 9 checks failed before diagnostics were added: `SSL - Memory allocation failed` at the handshake (`-32512`), a dropped connection, and one `fetch_failed` with no server request. Each recovered on the next scheduled attempt. Lowest-ever free heap during a check is 15-20 KB and the largest free block before a check ranged 32-49 KB, so the handshake is marginal. The check now logs heap before and after.
- `feedLoopWDT(): Failed to feed WDT! Error: 261` repeats while Wi-Fi connects: the loop watchdog is not armed yet. Harmless noise to be fixed.

- **Real host, first check (build 6 on `metarly.app`):** `SSL - Memory allocation failed` at the handshake with the largest free block at 32,756 B (free heap 82 KB, lowest 24 KB). Fix: a failed fetch retries after 2 minutes (up to 3 times in a row, then 30 minutes), and the render loop skips redraws while a check runs.

- **Real host, 304 (build 7 on `metarly.app`, issue #20):** `metarly.app` sends an `ETag` and answers `If-None-Match` with 304 (`curl -0`, HTTP/1.0, 2026-11-26 04:14 UTC). On the board, the first check 5 minutes after a plain reboot of build 7 (the stored ETag survives a reboot of the same build) logged `ota: check -> not_modified` (20:20:44 local). No Apache change needed.

## Run on 2026-11-26: network outages (issue #22, throwaway key, local HTTPS)

Setup notes: the server stands in for the Wi-Fi outage (refuse the TLS connection, stop the process, or cut the socket mid-image), so the board's radio stayed associated. The test certificate needs a `dNSName` of `192.168.4.67` for an IP host because mbedtls 2.x ignores IP SANs. The manifest `min_build` must stay above 9, and builds 3-5 are blacklisted from earlier tests. Reset the board with `esptool --after hard_reset chip_id`: toggling RTS/DTR by hand landed it in ROM download mode.

| Case | What was done | Result |
|------|---------------|--------|
| A. Network off before the update | Three reboots of build 10 with the server refusing the connection (A1), then stopped entirely (A2, A3) | **pass**: each first check logged `fetch_failed`; the board stayed on build 10 in app0, `pending:0`, blacklist unchanged at `[3,4,5]` |
| B. Dropped mid-download | Server closed the socket after 904,352 of 1,808,704 bytes | **pass after a fix** (below): `ota: check -> download_incomplete (build 11)`, no reset, still app0, other slot untouched. The next check, 30 minutes later, retried and installed |
| C. Network gone right after install | `ok_once`: the server sent the whole image, then refused everything | **pass** (one run): `installed (build 11)` at 08:25:16, reboot into app1, `new build confirmed` at 08:26:17 (+60 s), `pending:0`, blacklist `[3,4,5]`, no new entry |
| D. Armed watchdog under Wi-Fi loss | grep of the whole capture (911 lines, 4 reboots into build 10 and one into 11) | no `feedLoopWDT ... 261` and no panic after the fix. A Wi-Fi drop and return was not exercised on its own |

- **Bug found and fixed.** Case B first panicked (`Load access fault` in `TlsHttp::readImage`). Arduino's `HTTPClient::getStreamPtr()` returns `nullptr` once the peer has closed and nothing is buffered, and `readImage` and the manifest loop dereferenced it. Both now check for null; `readImage` returns 0, which `install()` reports as `download_incomplete` and aborts the update. Re-run on hardware: no panic.
- A `download_incomplete` waits the normal 30 minutes before the next check; only `fetch_failed` uses the 2-minute quick retry.
- Case C was run once, not across several reboots, and with the server dark rather than the Wi-Fi radio off.
- Not covered: switching the access point off at the moment the board starts a check.

## Run on 2026-11-26: power cuts (issue #23, throwaway key, local HTTPS)

Setup: the USB cable was pulled and replugged by hand (the board is USB powered). The server throttled the image (5 KB/s) so the download window was minutes wide. The window after `endUpdate` is milliseconds wide, so a temporary hook in `EspSlots::endUpdate` (never committed) logged `CUTNOW` and held the board for 10 minutes right after `Update.end()` succeeded, before `setActivated`; it fired once per build via an NVS flag. Replug shows reset reason `USB_UART_CHIP_RESET` rather than a power-on reset; the cut itself is real (the chip loses power with the cable).

| Cut point | What was done | Result |
|-----------|---------------|--------|
| 1. Mid-download | Cable pulled with the server at 512,000 of 1,808,960 bytes (~28%) | **pass**: booted build 20 in app0, `pending:0`, blacklist unchanged `[3,4,5]`, no rollback line |
| 2. Just after `endUpdate` | Cable pulled at `CUTNOW`, after the new image was written and activated in otadata | **pass**: booted the old build 22 in app0 (the bootloader treats a never-confirmed `PENDING_VERIFY` image as failed and falls back), no rollback line, `pending:0`, blacklist unchanged `[3,4,5]`, nothing blacklisted |
| Later install | Next check after each cut | **pass**: after cut 1 the next full download installed build 21 (confirmed); after cut 2 the next check installed build 23 (`installed`, reboot into app1, `new build confirmed`, `pending:0`, blacklist `[3,4,5]`) |

- Cut 2 was first attempted with a 60 s hold; the pull came after it expired, so the install completed and confirmed. It was repeated with a 10 minute hold.
- Cut 2 leaves a pending record but `state.activated()` is never set, so `onBoot` clears it without blacklisting, as designed. A cut *after* `setActivated` and before the 60 s confirmation is a different case: the bootloader still rolls back, and `activated == pending` makes `onBoot` blacklist that build. Not exercised here.
- Cable pulls hit the reset-to-ROM-download quirk once: `esptool --after hard_reset` left the board in download mode and a manual RESET tap was needed.

## Run on 2026-11-26: crash before setup() (issue #24, throwaway key, local HTTPS)

Setup: the board was on the real-key build 8, which rejects throwaway-signed images, so a throwaway-key build 30 was flashed over USB first (bootloader, partitions, `boot_app0`, app; NVS left as it was). Build 31 differed from 30 only by a temporary, never-committed `const int kCrash = crashInStaticInit();` in `src/hardware/ota_platform.cpp` (`-DCRASH_BEFORE_SETUP`), a namespace-scope static initializer that calls `abort()`, so it dies in the C++ constructors before `setup()` and before any `ota:` line or breadcrumb. The manifest offered build 31 as prod with `min_build` 0.

| Step | Observed | Result |
|------|----------|--------|
| Install | 5 min after boot: `check -> installed (build 31)`, `restarting into the new build` | pass |
| Crash | Boot of app1 prints `abort() was called` and `Rebooting...` with no `Plane Radar` banner and no `ota:` line: no breadcrumb, no `onBoot` | pass |
| Rollback | Bootloader booted app0 (build 30) on the next reset; `ota: build 31 failed and was rolled back; blacklisted` | pass |
| `/api/crash-log` | `{"reset":"panic","checkpoint":"ota_check","build":30,"pending":0,"blacklist":[31],"slot":"app0"}` | pass |
| Next check | `ota: check -> blacklisted (build 31)` (the first two checks, +5 and +7 min, were `fetch_failed` from the marginal handshake heap and retried on the 2 minute schedule; the manifest was fetched on the third) | pass |

- The `checkpoint` (`ota_check`, 300000 ms) in `/api/crash-log` is left over from build 30's own last run: the crashing build wrote none, which is the point of the case.

## Run on 2026-11-26: recovery-window boot delay (issue #25, real key, USB flash)

Setup: temporary, never-committed `millis()` stamps in `setup()` (entry, before and after `recoveryWindow()`, after `displayInit()`, after `wifiSetupConnect()`), printed once at 15 s. Board reset five times per variant with `esptool --after hard_reset`, no button pressed, joining the usual Wi-Fi. Variant B is the same build with `kOtaRecoveryWindowMs` set to 0.

| Stamp (ms since reset) | A. window 2500 ms (5 boots) | B. window 0 (5 boots) |
|------------------------|-----------------------------|------------------------|
| `setup()` entered | 302 | 302 |
| `recoveryWindow()` entered (after `Serial.begin`, `delay(500)`, `ota::init`) | 803 | 803 |
| `recoveryWindow()` returned | 2503 | 803 |
| `displayInit()` done | 2717 | 1017 |
| Wi-Fi connected, radar drawn | 4120, 4217, 4269, 4113, 7086 (outlier, slow join) | 2401, 2456, 2508, 2512, 2507 |

- **A normal boot is delayed by 1.70 s, not 2.5 s.** The window is `millis() < 2500`, counted from reset, so the 0.8 s `setup()` has already spent before it starts is part of it (identical on all 10 boots). Radar-on-screen goes from about 2.5 s to about 4.2 s with the window.
- **Can it shrink? Not recommended.** The press must come just after power-up (BOOT held through reset enters ROM download mode), and the window is the only time it is read. Cutting it to, say, 1.5 s would leave 0.7 s after `setup()` starts polling for a person to react to a power-up, to save about 1 s on a boot that already takes 4 s to the radar. A hold that has started inside the window keeps running past it, so only the press time is constrained. Left at 2500 ms.
- Not measured here: boot with the button pressed (the recovery run above covers that).

## Run on 2026-11-26: TLS heap margin (issue #26, real key, `metarly.app`)

Setup: a temporary, never-committed hook set the first check to 20 s after boot and called `ESP.restart()` right after it, so each boot was one real-host manifest check (304 or `not_modified`; no download). The same hook was used before and after the change, with the board on build 8.

| | Before (`main`) | After (this change) |
|---|---|---|
| Checks | 22 | 24 |
| `fetch_failed` (`SSL - Memory allocation failed`, -32512) | **7 (32%)** | **0** |
| Largest free block before a check | 16-61 KB, mostly 32,756 B | 36.8-65.5 KB |
| Lowest-ever free heap | 20-27 KB | 38-42 KB |
| Net task stack headroom | about 3 KB | about 3 KB |

- Before the change a failure did not track the largest block (one failed at 61 KB, one passed at 16 KB), so a minimum-largest-block precondition would not have helped.
- Change: `s_in_range` and `s_label_pending` (two `bool[8818]`) became `radar_geo::Bitset` (17.6 KB to 2.2 KB), and `Aircraft` shrank from 60 to 44 B (`gs_knots` and `pos_age_ms` to `uint16_t`, `alt[12]` to `alt[10]`, the `speed[10]` text replaced by `speed_kts` formatted at draw time). `.bss` went from 68,016 to 50,272 B.
- The 2-minute retry stays as the backstop. Ruled out: allocating the `Updater` only for a check (it is live during the check, so it frees nothing then), and a stack buffer for the OTA URL (`HTTPClient::begin` copies it into a `String`).
- Not measured: the download phase (10 KB low earlier, no install in this run) and checks at the usual +5 min after boot instead of +20 s.

## Run on 2026-11-26: crash at the top of setup() (throwaway key, local HTTPS)

Setup: the rig from the #24 run (throwaway key, test CA appended to the CA bundle, `kOtaHost` `192.168.4.67:8443`), built in a detached worktree off `main` at `b691d03`. Build 40 was flashed over USB. Build 41 differs only by `volatile int boom = 1; if (boom) abort();` as the first statement of `setup()` (before `crash_log::checkpoint`). A bare `abort()` there lets the linker drop the rest of `setup()`, including the build marker, and `ota_release.py sign` then rejects the image.

| Step | Observed | Result |
|------|----------|--------|
| Install | 20:28:46, 4 min after boot: `check -> installed (build 41)`, `restarting into the new build` | pass |
| Crash | `abort() was called at PC 0x420139b3 on core 0`, `Rebooting...` at 20:28:47, no `Plane Radar` banner, no `ota:` line | pass |
| Rollback | Bootloader booted app0 (build 40) on the next reset; `ota: build 41 failed and was rolled back; blacklisted` | pass |
| `/api/crash-log` | `{"reset":"panic","checkpoint":"ota_check","build":40,"pending":0,"blacklist":[31,41],"slot":"app0"}` | pass |
| Next check | 20:33:48: `ota: check -> blacklisted (build 41)` | pass |

- The `checkpoint` (`ota_check`) is left over from build 40's own last run: build 41 died before it could write `setup`, which is the point of the case.
- This matches the static-initializer variant above: a crash before `onBoot` ever runs is still recovered by the bootloader and recorded by the next good boot, with a single failed boot and no retry loop.
- Both checks in this run (the install and the `blacklisted` one) completed on the first handshake.

## Run on 2026-11-27: power cut inside the confirmation window (issue #35, throwaway key, local HTTPS)

Both runs used a 30-minute confirmation window (1800 s) so the cable could be pulled at leisure. The USB cable was pulled and replugged after build N+1 booted as `awaiting confirmation`.

| Firmware running the boot after the cut | Cable pulled | Result |
|---|---|---|
| main (build 52 to 53) | 55 s after boot | `build 53 failed and was rolled back; blacklisted`: a good build was lost. This is the bug. |
| #35 fix (build 54 to 55) | roughly 6 min after boot | Back on build 54; the next check at 09:05:02 installed build 55 again, so it was not blacklisted. The boot lines were not captured (USB was unplugged). |
| #35 fix plus the repeated log note (build 56 to 57) | 3 min after boot | Back on build 56 at 09:23:06 and logged `power was cut before the new build was confirmed; it will be tried again`. The check at 09:28:05 repeated the note and at 09:28:25 installed build 57 again. |

- The decision runs in the build the bootloader returns to, so only a baseline that already has the fix benefits.
- The fix treats `ESP_RST_POWERON` as a power loss. A brownout reset is still treated as a fault, so a cable pull that logs as a brownout on some boards would still blacklist.
- USB drops with the cable, so the boot lines can be missed (the first fix run missed them). The `ota: check starting` line therefore repeats the power-cut note.
- A bad build that hangs and is then power-cycled by hand is retried rather than blacklisted. The armed loop watchdog normally resets a hang first.

## Still open

- Case C across several reboots; Wi-Fi radio actually off (AP powered down) during a check and after an install.
