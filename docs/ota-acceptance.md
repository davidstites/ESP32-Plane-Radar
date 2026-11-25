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
