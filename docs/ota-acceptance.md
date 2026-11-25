# OTA on-device acceptance

Status: **procedure only; no step has been run on hardware yet.** Results go in the table at the bottom.

Setup: a C3 on USB, the firmware from this branch built with a throwaway release key in `keys.h` (never the real one), a local HTTPS server using a throwaway CA, and `-DPLANE_RADAR_BUILD=N` / a temporary `config::kOtaHost` pointing at the server. `scripts/ota_release.py` signs the manifests. Spike helpers are in `git stash` (`ota-spike`), reference only.

Flashing the C3: hold BOOT, tap RESET, release BOOT fully, tap RESET again.

| # | Step | Pass when | Result |
|---|------|-----------|--------|
| 1 | Flash over USB | log shows `ota: build N, device <hash>, slot app0, store ok`; radar draws | pending |
| 2 | Good update (build N+1) with the radar running | installs, reboots into app1, logs `new build confirmed`; lowest-ever heap and net-task stack headroom recorded; headroom >= 1 KB (else raise the `net` task stack or pause rendering during the check) | pending |
| 3 | Bad update (image that panics at boot) | bootloader rolls back; `/api/crash-log` lists the build under `blacklist`; next check reports `blacklisted` | pending |
| 4a | Hang in `loop()` | loop watchdog resets; rolls back | pending |
| 4b | Hang that blocks the `net` task | no confirmation within 10 min; board restarts; rolls back | pending |
| 5 | Release candidate | list the board in the prod manifest, serve `rc/<hash>.json`: board installs it (`candidate`); `promote` the same bytes, board reports `already_current` | pending |
| 6 | Real host | one manifest fetch against `metarly.app` validates with the Mozilla bundle (needs hosting confirmed) | pending |

Not covered by this list and worth a look: power cut mid-download, crash before `setup()`.

## Added after the final review (unverified on hardware)

- The manifest GET now uses HTTP/1.0 (`useHTTP10`) so a chunked response cannot appear; confirm a 200 and a 304 against the real host still parse.
- Confirmation is local only: a new build confirms after 60 s of running (`ota: new build confirmed`), with no network involved. Test it with Wi-Fi off before, during and after an update, across several reboots: it must confirm and never roll back. A crash or hang inside that minute (e.g. a deliberate `abort()` in `loop()`) must roll back and be blacklisted.
- Base path is `/ota/planeradar` (manifest, `firmware-N.bin`, `rc/<hash>.json`).
- Recovery button: after an update, power-cycle and hold BOOT (pressed just after power-up, never through reset) for 3 s; expect `ota: recovery button held` and a restart into the previous build, which must not reinstall the blacklisted one. With only one image installed expect `no previous firmware to return to` and a normal boot. A normal boot with no button must not be delayed beyond about 2.5 s.
