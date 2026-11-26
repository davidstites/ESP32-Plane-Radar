# OTA on-device acceptance

Status: steps 1-6 and the recovery button have been run on hardware (2026-11-25), and the Wi-Fi outage cases (issue #22) on 2026-11-26; the items under "Still open" have not. Results are in the table below.

Setup: a C3 on USB, the firmware from this branch built with a throwaway release key in `keys.h` (never the real one), a local HTTPS server using a throwaway CA, and `-DPLANE_RADAR_BUILD=N` / a temporary `config::kOtaHost` pointing at the server. `scripts/ota_release.py` signs the manifests. Spike helpers are in `git stash` (`ota-spike`), reference only.

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

Not covered by this list and worth a look: power cut mid-download, crash before `setup()`.

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

## Still open

- Case C across several reboots; Wi-Fi radio actually off (AP powered down) during a check and after an install.
- Power cut mid-download; crash before `setup()`; boot delay from the 2.5 s recovery window.
