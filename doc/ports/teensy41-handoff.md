# Development handoff — settings and text apps complete, 2026-09-27

Repository: `/home/dennis/Documents/SuperKeyboard/Code/solar_os`.
Preserve all uncommitted changes. No reset, clean, push or deletion of user files.
No sub-agents are authorized. User selected persistent settings, less, Notes and
Sheet; implementation and hardware validation are complete. No further priority
has been selected. Serial test processes have exited; monitor may reopen.

## Installed firmware

Profile: `teensy41_apps`, extending Files/SSH/network/audio/shell/MicroPython.
HEX: `/tmp/solaros-apps-build/teensy41_apps/firmware.hex`.
SHA256: `2c87fe4d743aa13c3298b222cadb0df6da604174e8df37895e3208f88dc5aafb`.
RAM1 423040, RAM2 182672, program flash 848396 bytes.
Permanent backup: `../solar_os-baselines/2026-09-27-apps/` (HEX/ELF, logs, tracked
patch, source archive). Previous committed baseline: `b14f61d`. The user requested
committing the completed SSH, Files, settings and text-app work together; the
commit containing this handoff records that tested state. Prior Files/SSH/flash
backups are retained. No push was requested.

## What changed

- Flash-backed bounded NVS compatibility in `settings.c`; snapshots under
  `/flash/.solar-settings`. CRC/version validation, staged writes, sync followed
  by LittleFS atomic replacement, unchanged-write suppression and explicit errors.
  Four handles, 16 keys per namespace, u8/u16/strings <=63 bytes. Single console
  owner; simultaneous opens of a namespace are rejected. No SD fallback/format.
- Shared identity service replaces hard-coded shell and SSH names. Commands:
  `identity [status]`, `identity user NAME`, `identity hostname NAME`.
  Hostname changes OS identity, not Ethernet DHCP/mDNS configuration.
- `setterm size COLS ROWS` now persists. `setterm` shows geometry/startup path;
  `setterm startup auto|flash|sd` saves the shared selection. Startup runs once
  per boot on first USB shell connection. No startup file is auto-created.
- Unchanged shared less, Notes and Sheet are enabled. Notes is a Markdown
  checklist; Sheet is a CSV viewer with formulas, not a cell editor. Files can
  return from Sheet and F3/less. Shared input widget now accepts CR and LF Enter.
- Manual generator handles mutually exclusive #if/#elif/#else table entries,
  still rejecting overlapping duplicate entries. Only enabled app references
  are embedded, e.g. `less man:app.notes`.

## Passed validation on installed image

- `/tmp/teensy-apps.json`: saved user/hostname/100x30/startup flash across reboot,
  restore and reboot, startup once per boot and not on reconnect; less search,
  Notes saves, Sheet quoted CSV/formulas on both SD and flash; missing files;
  Files child return; 20 cycles of all three apps plus settings writes. Heap and
  PSRAM stable at 44556 and 8385240 bytes free; flash handles zero.
- `/tmp/teensy-apps-files.json`: full copy/move/recursive copy/ZIP/cancel and
  editor/Python child return regression.
- `/tmp/teensy-apps-ssh.json`: full password login/I/O, bad auth, changed host key,
  cancellation/peer drop/stalled handshake and repeated memory cleanup.
- `/tmp/teensy-apps-network.json`: TCP/UDP, quotas/timeouts/cancellation, stale
  handles, interpreter cleanup and network restart.
- `/tmp/teensy-apps-flash-audio.json`: SD/flash file modes/hashes/PSRAM buffer
  stress/recursive Python file I/O/editor/descriptor cleanup and MP3/WAV playback
  from flash with Ethernet active. Final heap 44772/93024, PSRAM 8385240/8388608,
  flash open=0. Recursion stress retained 1925 console stack words.
- Settings transaction/failure/corruption/bounds host tests and child lifecycle
  ASAN/UBSAN tests; all 14 manual-generator tests, shared widget CR regression,
  core/parser/path/cursor tests. Previous `teensy41_files` profile still builds.

## Current device state and reproduction

Preferences restored: `user@teensy41`, size 80 24, startup auto. Temporary startup
script removed. Unique `_apps_*`, `_solaros_files_*` and `_solaros_flash_*` fixtures
remain. Ethernet up at 192.168.1.197. Serial monitor can reopen:
`pio device monitor --baud 115200 --raw --exit-char 28`.

Build/upload: `PLATFORMIO_BUILD_DIR=/tmp/solaros-apps-build pio run -e teensy41_apps`
(add `-t upload`). Close serial monitor first. Use separate build dirs and
preserve known-good HEX/ELF before profile changes. Loader sometimes retries a
USB write and then succeeds. It rejected the initial >1 MiB image at the Intel
HEX boundary; focused manual pages keep this image below that limit. Investigate
the loader before a future larger image; board flash capacity is not the limit.

Hardware suite: `/tmp/solaros-ssh-testenv/bin/python scripts/ports/test_teensy41_apps.py --log /tmp/teensy-apps.json`.
It restores preferences and only creates a missing startup file. Never run serial
tests concurrently. After interruption, `--restore-from OLD_LOG` with a different
`--log NEW_LOG` restores the original settings. Reboot clients must wait for
`rebooting` acknowledgement before closing USB DTR; otherwise the command may
execute only when the next connection opens. MicroPython has no `os` module.

Host settings: `bash scripts/ports/test_teensy41_settings_host.sh`.
Host child lifecycle: `bash scripts/ports/test_teensy41_children_host.sh`.
ASAN leak checking requires execution outside this host's restricted sandbox.

## Remaining scope

Power-loss/endurance and cold power-cycle validation of the new settings store
remain separate from reboot and injected-failure tests. More service preferences,
full NVS inspection/backup commands, physical displays/keyboard, clock/time,
SD hot-removal, cross-volume directory moves, full sessions/background workers,
SSH public-key/server interoperability and broader Python modules remain future
work. See `teensy41.md` and `teensy41-roadmap.md` for details and prior fixes
(MPU main stack boundary, flash PSRAM staging, full newlib, worker priorities).
