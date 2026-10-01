# Teensy / SuperKeyboard handover

Updated 2026-10-01. Branch: `teensy41`; GitHub: `dennis1a4/solar_os`.
This is the current state. Older snapshots are in the
[handover history](teensy41-handoff-history.md).

## Installed firmware and wiring

Installed **`teensy41_telnet_legacy`**, with **AmpEn on pin 40**, Serial1 retained
and physical scope ADC disabled. Keep this profile until the AmpEn0/ADC40 move.
The normal `teensy41_display` image requires that wiring change and disables
Serial1. Both scope and USB-PD demo apps are available on the installed image.

Installed HEX SHA256:
`bd46884e615f35e899cc6d3372d0836d97f45597a12a4d5517937b125517603a`.
Flash 1,416,488 bytes; RAM1 438,880; RAM2 340,848.
Microsoft keyboard `045e:0750`, powered USB host hub, RA8875 LCD, native SD,
USB drive, QSPI flash and 8 MiB PSRAM. Audio shield was absent in recent tests.
See [quick-start and wiring](README.md).

## Latest addition: PSRAM RAMFS (original item 7)

Installed quota-backed temporary mounts using shared SolarOS RAMFS:
`ramfs mount /ram 1m`, ordinary file/app paths, `df`, Files/Edit, Python,
archives and cross-volume transfers. No automatic mount; reboot/unmount loses
contents. Up to four top-level mounts, 1 KiB–4 MiB each; 512 KiB PSRAM admission
reserve. Busy file/directory handles block unmount, including background Python.
Unknown root paths no longer implicitly target SD: use explicit `/sd/...`.
See [RAMFS usage, API and limits](teensy41-ramfs.md).

Host ASan/UBSan and eight manual tests pass. Main device acceptance passed:
`/tmp/teensy-ramfs-device.json`; five cycles recovered exactly 28,516 internal /
8,158,704 PSRAM bytes free. Supplemental device evidence is
`/tmp/teensy-ramfs-edges.json` passed (editor, LCD, four mounts, flash, reboot).
The reboot test leaves no RAMFS mounts or retained test apps. Ethernet may need
`network up` again. No wiring changes were made.
Recovery checkpoint: `../solar_os-baselines/2026-10-01-ramfs/`.

The original approved workstation sequence (1, 2, 3, 4, 7), plus reusable Tab
completion, is implemented within the documented port limits. No next feature
has been selected. Earlier work is committed/pushed as `d6a16f9`; unrelated DNP3
files remain untouched and excluded from commits/checkpoints.

## Previous addition: stage 4 hardware resources

Installed `gpio`, `i2c`, `spi`, `uart`, `expansion`, inspection `io`, and shared
resumable `com`, with fixed board pin reservations and atomic UART claims.
See [hardware commands, limits and validation](teensy41-hardware-resources.md).
Host sanitizer and seven manual tests pass. Final device acceptance passed,
including physical UART8 RX34/TX35 loopback through raw UART and COM, USB/LCD
ownership, suspend/resume, disconnect cleanup and exact five-cycle memory
recovery (28,644 internal / 8,160,108 PSRAM free). UART8 is closed; user was told
the jumper can be removed. External SPI/I2C devices are not validated.

Recovery checkpoint: `../solar_os-baselines/2026-09-30-hardware/`.
PSRAM RAMFS followed this checkpoint; see current state above.

## Previous addition: shared Tab completion

The completion checkpoint added cursor-aware completion to the Teensy shell: command/
app/alias names, paths (including quoted spaces), directory-only `cd`, retained
session IDs, active job IDs/names and supported settings. Ambiguous second Tab
shows at most 20 entries and a remaining count. Only the requested directory is
scanned, with no filesystem index. Filesystem iteration yields between entries.

Shared C APIs provide a pluggable provider registry, shell-line completion,
raw-field completion for dialogs, and a TUI input-widget adapter. Existing app
fields opt in; Python/Lua language bindings were not added. See
[completion usage/API](teensy41-completion.md). The host sanitizer, shared widget,
and existing shell completion tests pass. Final device evidence is
`/tmp/teensy-completion-device.json` (passed). Five cycles recovered exactly
29,188 internal / 8,163,984 PSRAM bytes free. The recovery checkpoint is
`../solar_os-baselines/2026-09-30-completion/`.

Stage 4 is now installed; see the current state above.

## Stage 3: network diagnostics and clock synchronization

`ping HOST [COUNT]`, bounded TCP `netscan`, and `ntp [-q] [SERVER [PORT]]`
are installed. See [network diagnostics](teensy41-network-diagnostics.md) for
syntax, limits and validation. The existing Ethernet task owns ICMP/UDP/TCP
operations; commands yield other consoles and support cancellation. NTP validates
replies and preserves timezone settings. It is plain, unauthenticated NTP, not an
automatically running clock discipline.

Host ASan/UBSan protocol tests and six embedded-manual tests pass. The device
acceptance passed: `/tmp/teensy-netdiag-device.json`. Five repeated cycles
recover exactly 29,124 internal / 8,163,984 PSRAM bytes free. The test synchronizes UTC to
the host computer, leaves Ethernet up and preserves the saved timezone. Recovery
artifacts are in `../solar_os-baselines/2026-09-30-netdiag/`.

Stages 4 and RAMFS have since been completed.
Stage 1/2/2b/3 changes are included in commit `d6a16f9`.

## Detachable MicroPython processes (stage 2b)

Implemented and installed: standalone text Python runs on an admitted 40 KiB
OCRAM worker, with private directory/input/output, Ctrl+Z, `bg [ID]`, numeric
`jobs` entries, `fg ID` across consoles, and cooperative `job stop|kill ID` /
`close ID`. `tail [-n N] FILE` inspects a snapshot. One VM remains the explicit
limit; completed detached jobs retain output until reaped. Foreground/suspended
Python cancels on disconnect; explicitly detached Python survives disconnect.
Other resumable apps remain paused sessions. Lua, CAN/DNP3/DAQ/stream bindings
are future work, not supplied by this worker. See [process jobs](teensy41-process-jobs.md)
and [example logger](../../examples/teensy41/background_logger.py).

Final-image evidence:
- `/tmp/teensy-process-device.json`: logger with Calc/tail, safe pause/bg/fg,
  cross-console reattach, USB disconnect survival, partial input, singleton
  admission, suspended stop and eight CPU-loop cancellation cycles. Exact warm
  memory recovery: 29,020 internal / 8,163,984 PSRAM free.
- `/tmp/teensy-process-edges.json`: REPL interrupts/partial expression, completed
  job reattach alongside ZIP, output truncation, detached graphics rejection,
  foreground disconnect cleanup and tail boundaries.
- `/tmp/teensy-process-network.json`: existing Python DNS/TCP/file transfer,
  timeout, Ctrl+C, GC/socket cleanup and restart regression passes.
- `/tmp/teensy-process-graphics.json`: foreground Python LCD graphics, rejection
  of graphics suspension, Ctrl+C cleanup and exact warm memory recovery.
- Existing session/job sanitizer regressions and six embedded-manual tests pass.

Native Python file calls release the console gate around the storage-locked
operation. Suspended stacks are retained at safe boundaries. Cancellation is
cooperative: a script that catches interrupts may remain `stopping`; no live
worker is forcibly deleted. Foreground worker creation cannot reap a completed
Python worker owned by its process record.

Recovery snapshot: `../solar_os-baselines/2026-09-30-process-jobs/`.
This is the prior firmware recovery point. Unrelated untracked DNP3 work remains
untouched and outside the tested port.

## Background shell jobs and scheduling (stage 2)

Stage 2 is implemented, installed and device-tested. Four cooperative script jobs (`jobs`, `job`) and the shared persistent
`schedule` service are available. Host lifecycle, scheduler, timezone conversion,
and existing Clock tests pass. USB/LCD jobs, 24-cycle exact heap recovery,
Clock suspend/alarm/resume, schedule triggers/skips and persistence across reboot
pass (`/tmp/teensy-jobs-device.json`). See [jobs notes](teensy41-jobs.md).

The first jobs image failed at startup after crossing an ITCM bank boundary.
A button-assisted reflash recovered the board; moving compiler helpers to
cached flash restored 32 KiB RAM1. The linker now rejects less than 72 KiB
between static data and the core stack top, including the 8 KiB stack guard.
The user requested no pin-assignment work; existing build wiring is retained.
Next is stage 3: network diagnostics/time sync, after the user is ready.
Recovery snapshot: `../solar_os-baselines/2026-09-30-jobs/`.

## Completed work and evidence

- Retained app sessions added 2026-09-30: Ctrl+Z, `fg [ID]`, `close ID`, four
  suspended app chains per console, owner-task requests and disconnect cleanup.
  Host sanitizers, USB/LCD state/memory checks, graphical Plot resume and Telnet
  retention/disconnect regressions pass. User confirmed calculator retention and Plot redraw. Dynamic shell creation/migration is not included. See
  [session notes](teensy41-sessions.md).

- Keyboard repeat: 400 ms initial delay, 33 ms interval; release/disconnect and
  app-transition cancellation. User confirmed letter, Left, Backspace and Shift
  repeat/release. Diagnostics recorded 144 repeats with zero drops. Host
  ASan/UBSan and dual-console device regression pass. See [keyboard notes](teensy41-keyboard.md).
- Workstation commands: embedded Help/Man, Watch, version/board/status/pwd,
  task Top, Port, DF, Date/Time, ZIP/Unzip and Ethernet Curl. Session/Sessions
  now list fixed consoles and retained app chains. Host and device suites pass; remote man/watch/
  session and Telnet lifecycle checks pass. See [audit and backlog](teensy41-workstation.md).
- Telnet: authenticated incoming shell, one client; device lifecycle checks
  pass and the user confirmed a real connection. See [Telnet notes](teensy41-telnetd.md).
- Graphics, View, Python graphics, Invaders and MQTT Explorer have recorded
  device/user checks. SD recovery is implemented and host-tested; physical
  SD removal remains unverified. Clock host/remote checks pass. CAN/OBD, scope
  and USB-PD have software/demo coverage; their hardware backends or validation
  remain incomplete. Follow the [master test checklist](teensy41-test-checklist.md).

The earlier stage 1 session upload rebooted the board. USB/LCD and Telnet regression checks
passed afterward. Ethernet was started for Telnet testing; its temporary listener
and password file were removed at the end. USB/LCD have no retained test apps.
No media eject or shutdown was performed. No RTC/timezone change was made during
that earlier session/keyboard testing; stage 3 now synchronizes UTC via NTP. Use `date YYYY-MM-DD`
and `time HH:MM:SS` to set local time, or the documented `rtc` UTC interface.
Correct UTC is required for HTTPS. Saved Manitoba timezone is fixed UTC-5.

## Completed sequence and next work

The user-approved sequence is complete, within the limits in each feature note:

1. Retained app sessions — implemented and automated acceptance passed; user confirmed Ctrl+Z, calculator retention and Plot repaint. Editor visual checks remain optional follow-up.
2. Background jobs and scheduling (`jobs`, `job`, `schedule`) — implemented; host/device acceptance passes.
   Stage 2b detachable MicroPython (`bg`/`fg`, input/output, safe stop, tail) also passes final-image device tests.
3. Network diagnostics and clock sync (`ntp`, `ping`, `netscan`) — implemented; see the stage 3 notes above.
4. Hardware resource management and serial terminals (GPIO/buses, `io`,
   `expansion`, `com`, with pin ownership) — installed and validated.
5. PSRAM temporary storage (`ramfs`, item 7 of the original recommendation) — installed and validated.

No new feature is selected. General pipes/redirection, monitoring and file-transfer
features remain separate backlog, not implicit POSIX support.
Cold DF scan cooperation, physical SD removal, Clock audio, scope wiring/ADC,
STUSB4500 and CAN prerequisites remain in the master checklist.

## Recovery and repository state

Local firmware/source/log checkpoints are in `../solar_os-baselines/`:
`2026-10-01-ramfs/` is the installed RAMFS build;
`2026-09-30-hardware/` is the prior hardware-command build;
`2026-09-30-completion/` is the earlier completion build;
`2026-09-30-netdiag/` is the prior diagnostic build;
`2026-09-30-process-jobs/`, `2026-09-30-jobs/`, and `2026-09-30-sessions/`
are earlier workstation checkpoints;
`2026-09-29-keyboard/` is the previous keyboard build;
`2026-09-29-workstation/` is its predecessor. These artifacts are not committed.
The keyboard snapshot predates the final user confirmation; current docs record it.

The prior integrated port checkpoint is committed/pushed as `d51761a` on
`teensy41`. Current stage 1/2/2b/3 changes are uncommitted. Unrelated unfinished `solar_os_dnp3_bridge.*` and `src/vendor/opendnp3/`
remain local, untracked and outside the tested port. Do not resume DNP3 implicitly.
Unique SD workstation fixtures remain for inspection; no user files were removed.

For regression commands, see the [test guide](../../scripts/ports/README.md).
Run hardware suites one at a time with exclusive USB and an idle local keyboard.
Session implementation was built/flashed on the confirmed AmpEn40 wiring.

Cleanup validation: ten host suites passed (keyboard, child lifecycle, Clock,
graphics, MQTT, OBD, USB-PD, scope, SD recovery and Telnet), plus 14 shared manual
tests, 13 port manual/hotplug tests, 29 package tests and the linked USB DMA buffer
placement check against the installed ELF. Documentation links and diff whitespace
checks passed. Restored the core power service in the package manifest; the new
USB-PD service remains scoped to its own package.

Session evidence: `/tmp/teensy-sessions-device-final.json`,
`/tmp/teensy-sessions-graphics.json`, `/tmp/teensy-sessions-telnet.json`.
USB lifecycle checks recovered exactly 29,948 internal and 8,202,420 PSRAM bytes.
The first device attempt needed prompt-aware test waiting; another corrected the
expected singleton-conflict message. Final device checks passed. The final
firmware additionally invalidates the graphics presenter on text-shell return;
graphical/Telnet checks ran on that final image.
