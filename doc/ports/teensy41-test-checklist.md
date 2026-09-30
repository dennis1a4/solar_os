# Teensy / SuperKeyboard outstanding test checklist

Updated 2026-09-29. **Use this as the master test queue.** Feature notes linked
below contain procedures and historical evidence; the roadmap tracks development.
Unchecked items are outstanding, not failed unless explicitly described as such.
Do not check off a physical test based on simulation, injected input or a build.

## Firmware and wiring first

| Image | State | Relevant wiring / coverage |
| --- | --- | --- |
| USB-storage baseline | Historical, preserved checkpoint | Earlier physical USB/keyboard checks passed here; no SD recovery or Clock |
| Clock image | Prior validated checkpoint | Includes SD recovery and OBD demo; AmpEn is still pin 40; Clock remote checks passed |
| Workstation + keyboard legacy image | Currently installed; workstation/Telnet/dual-console tests and basic physical repeat passed | AmpEn40, scope ADC disabled, Serial1 retained; includes scope/pdpower demos |
| Normal display candidate | Built, not flashed | Telnet plus scope ADC40/AmpEn0; Serial1 UART disabled; USB/LCD retained |

- [ ] **SETUP-1 — Pending pin move:** move AmpEn to pin 0 before installing the
  scope candidate. Do not attach scope input to pin 40 while the old firmware
  still drives that pin as AmpEn. Record the actual wiring and firmware hash.
- [ ] **SETUP-2 — Candidate boot regression:** after rewiring/upload, verify LCD
  and USB shells, keyboard, powered hub, `/sd`, `/usb`, `/flash`, Ethernet and
  app listing. Confirm AmpEn is on pin 0 and pin 40 is an input. Serial1 on
  pins 0/1 is intentionally unavailable in this candidate.

The SD, Clock and OBD demo tests can begin on the installed legacy image without
waiting for the scope wiring. Use the powered host hub. Keep older checkpoint
files with their corresponding wiring descriptions.

## Keyboard and host hub

Keyboard repeat is implemented; see [timing and policy](teensy41-keyboard.md).
Host sanitizer and dual-console device tests pass. The user confirmed physical
repeat/release for letters, Left, Backspace and Shift changes on 2026-09-29.
Broader app/multiple-key and reconnect checks below remain pending.

- [x] **KEY-1 — Implement repeat first:** initial delay, repeat interval, key
  release/disconnect cancellation, and a defined policy for multiple held keys
  and modifiers. 400 ms delay, 33 ms interval; newest key wins.
- [ ] **KEY-2 — Hold/release:** hold letters, arrows and Backspace at the shell
  and in Edit/Files. Verify first press, delayed repeats, steady rate, immediate
  stop on release, and no repeated modifier events. Check shifted characters,
  changing keys and simultaneous holds; watch for queued repeats after release.
  Basic shell letter/Left/Backspace/Shift repeat and release confirmed by user;
  Edit/Files and simultaneous holds remain pending.
- [ ] **KEY-3 — Disconnect while repeating:** unplug while holding a letter or
  arrow; release before reconnecting. No phantom input while absent, no repeat
  after reconnect, and the first new key works. Repeat with Shift/Ctrl held.
- [ ] **KEY-4 — App interaction:** repeat/navigation in Files/Edit and movement/
  firing in Invaders remain usable. App exit/switch does not deliver stale keys
  to the next shell/app. Injected keys are not physical-key validation.
- [ ] **KEY-5 — Compatibility:** retest the home-built keyboard that previously
  failed enumeration, and another keyboard/HID layout. Record VID/PID and result.
- [ ] **HUB-1 — Shared hub:** reconnect keyboard with drive present, then reconnect
  the entire powered hub with no writes active. Both devices recover; no stuck
  modifiers, storage handles or queues. Boot with both already attached.
- [ ] **HUB-2 — Repeated cycles:** at least 20 physical reconnects for keyboard and
  drive; record failures, mount state and idle memory, not just the final cycle.

Detailed observations/procedure: [physical reconnect tests](teensy41-hotplug-tests.md).

## SD and USB storage

**Ready on installed firmware.** SD recovery host tests and boot mounting passed;
physical SD recovery remains unverified. USB safe removal and read-only stale-
handle recovery passed once on the old USB baseline; repeat them on the final image.

- [ ] **SD-1 — Boot/insertion:** boot without SD, then insert; boot with SD already
  present. `/sd` appears, files read correctly, both shells remain usable. Repeat
  after upload/cold start to investigate the historical intermittent mount issue.
- [ ] **SD-2 — Safe eject:** `sd eject`, remove/reinsert, verify mount listing and
  all bytes in a unique fixture. Eject refuses live file/directory handles.
- [ ] **SD-3 — Read-only surprise removal:** retain a file handle, remove SD,
  confirm old operations fail and both consoles stay responsive. Reinsert before
  closing that handle: remount must wait for stale handles to close. Reopen and
  verify the fixture. `/flash` and `/usb` stay usable.
- [ ] **SD-4 — Replacement/cycles:** repeat with a different card; no stale handle
  accesses the replacement. Run at least 20 physical cycles. Rapid swaps missed
  by removal detection are not established as supported.
- [ ] **USB-1 — Current-image regression:** safe eject, surprise read-only removal,
  stale-handle remount blocking, close/recovery and hash-checked reopening.
- [ ] **USB-2 — Media matrix:** another drive/capacity and supported FAT/exFAT
  formats; record exact partition layout. GPT/unpartitioned media are unverified,
  not promised supported. Keep drive testing separate from filesystem claims.
- [ ] **STOR-1 — Cross-volume/app checks:** root listing and Files panes, copy/move,
  append/seek/rename, editor saves and Python file handles across SD/USB/flash.
  Include missing/full/read-only media and directory handles. Verify error paths
  leave no busy handles and do not overwrite unrelated files.
- [ ] **STOR-2 — Active-I/O removal:** separately test read/write interruption on
  disposable media. Check for hangs and bounded recovery; filesystem integrity
  during interrupted writes is not guaranteed. SD FIFO inner waits remain a
  known concern. Do not mix this with the read-only fixture tests.
- [ ] **STOR-3 — Performance/soak:** measure SD throughput with the new per-sector
  guards and run a longer mixed USB/SD soak. The previous 1,000-cycle run lasted
  only 154 seconds; it does not establish hours of reliability.

Procedures: [SD recovery](teensy41-sd-recovery.md), [USB storage](teensy41-usb-storage.md).

## Clock, timezone and audio

**Clock is installed; the following remote Clock passes were on the prior Clock image.** Remote frame activity, stopwatch controls, countdown
completion, cleanup, five launch/exit memory cycles, and Manitoba/RTC retention
through software reboot passed. They do not establish visual or audible behavior.

- [ ] **CLOCK-1 — LCD:** visually check `clock`, `clock -s`, `clock -a 00:10`;
  readable digits, correct local time, blinking colon, pause/reset, zero and exit.
- [ ] **CLOCK-2 — Sound:** with audio hardware restored, hear countdown alarm,
  verify cadence and silence on exit. Check alarm alongside PCM playback and
  with the shield absent; playback should retain priority.
- [ ] **CLOCK-3 — Power loss:** verify saved Manitoba setting across a full power
  cycle, and RTC retention with the intended backup battery. Confirm UTC RTC vs
  displayed UTC−5. Software-reboot retention already passed.
- [ ] **AUDIO-1 — Integrated hardware:** restore the shield and test playback,
  recording, synth and alarm with LCD/USB/SD active and the new AmpEn wiring.
- [ ] **AUDIO-2 — Quality:** investigate the recorded 60 Hz microphone hum; verify
  clean capture and levels on the eventual custom-board audio circuit.

Details: [Clock](teensy41-clock.md), [synth/audio](teensy41-synth.md).

## Scope

**Blocked on rewiring/upload/front end for real measurements.** Host model and
GUI tests/build passed; ADC/DMA timing, accuracy and physical display remain untested.

- [ ] **SCOPE-1 — Demo/LCD:** `scope --demo`; sine/square/DC, grid and text,
  all timebases, trigger level/edge, auto/normal, single shot, run/hold, ranges,
  exit/relaunch. Confirm normal mode waits on DC and single shot holds one trace.
- [ ] **SCOPE-2 — ADC baseline:** pin 40 grounded, then known safe ADC-level DC
  and a low-voltage periodic source; verify raw 0–3.3 V readings and trace.
- [ ] **SCOPE-3 — Front-end ranges:** verify the physical 5 V/50 V jumpers and
  matching positive-only software scaling against a meter. Check endpoint
  clipping indication and coarse accuracy after front-end protection is validated.
- [ ] **SCOPE-4 — Timing/trigger:** compare known signal frequencies across sample
  rates/timebases; verify trigger alignment, RMS and Vpp estimates. Characterize
  distortion/aliasing and snapshot dead time; do not infer performance from the
  displayed nominal timer rate. No serial decoding is implemented.
- [ ] **SCOPE-5 — Resource cleanup/coexistence:** repeated launch/hold/exit while
  USB/SD/Ethernet/audio operate; DMA channel, timer and heap recover, no freezes.
  Inject capture failure/timeout where practical. Concurrent on-chip ADC consumers
  require ownership work before they are supported.

Details and pin requirements: [scope](teensy41-scope.md).

## USB-PD and CAN/OBD

- [ ] **PD-1 — Demo on device:** `pdpower --demo` (now installed) on USB
  and LCD: profile/current selection, confirm/cancel, success/reject/timeout/
  disconnect/I/O-error/mismatch scenarios, repeated app cleanup. No real voltage
  changes are implemented in this version.
- [ ] **PD-2 — Hardware prerequisites:** implement STUSB4500 backend; establish
  I2C pins/address, regulator limits and contract observation before real tests.
- [ ] **PD-3 — Hardware negotiation (after PD-2):** supported/unsupported profiles,
  contract confirmation, charger disconnect/reconnect, communication loss,
  startup at 5 V and stable regulated rails through transitions. Motor controller
  work and NVM programming are deferred, not available features awaiting testing.
- [ ] **OBD-1 — Installed demo:** `obd --demo` on LCD/USB; ECU/category navigation,
  simulated clear confirmation/rescan, fault scenarios and exclusive report saves;
  verify cleanup and readability. Host tests passed; device demo tests pending.
- [ ] **CAN-1 — Hardware prerequisites:** implement MCP2515 backend and resolve
  temporary slot pin conflicts; display slot 0, expansion slots 1/2. The separate
  `canmon` app/relay mode is not implemented.
- [ ] **CAN-2 — Bench hardware (after CAN-1):** controller loopback, two-node
  traffic, bitrate/termination, ISO-TP timeouts, bus-off/recovery and interface
  disconnect. Validate diagnostic reads before separately testing clear-code
  behavior on an explicitly selected test ECU. Relay testing awaits its app.

Details: [PD](teensy41-power.md), [OBD](teensy41-obd.md), [CAN plan](teensy41-can-obd-plan.md).

## Telnet server

- [x] **TEL-1 — Host protocol/authentication:** streaming negotiation, malformed
  input, login timeout, password rejection, short/stalled writes and reconnects.
- [x] **TEL-2 — Real Ethernet sessions:** wrong login, initial/live window size,
  independent cwd, graphical-app rejection, busy peer, Files and running Python
  cleanup, remote exit, ten reconnects, Ethernet software down/up and three
  immediate listener restarts. Warm idle internal/PSRAM readings matched exactly.
  Evidence: `/tmp/teensy-telnet-device-final.json`, legacy image, 2026-09-28.
  The user also confirmed their own Telnet connection worked.
- [ ] **TEL-3 — Physical/longer coverage:** physical Ethernet cable removal,
  hours-long sessions/traffic, additional terminal clients, slow-reader behavior
  on actual TCP and remote audio coexistence. Host stalled-write testing is not
  an actual-network backpressure test.
- [ ] **TEL-4 — Failure/limits:** allocation/socket exhaustion and abrupt power
  loss/reboot; confirm listener defaults off and requires explicit startup.

Usage, firmware distinction and evidence: [Telnet server](teensy41-telnetd.md).

## Whole-system regression and longer-term board tests

- [ ] **SYS-1 — Existing apps after new firmware:** smoke-test Calc, Edit, Files,
  less, Notes, Sheet, Python, Plot/Playground, View, Invaders and MQTT Explorer.
  Check LCD/USB session separation, cancellation and graphics/text restoration.
  Earlier graphics/View/Invaders and MQTT visual checks passed; these are regression
  checks for the new combined image, not claims of never-tested features.
- [ ] **SYS-2 — Memory/stack:** baseline idle heap/PSRAM, sample during each app,
  repeat launch/exit, measure task stack headroom and allocation-failure paths.
  Validate full-capacity PSRAM beyond previous small-buffer checks. Do not
  destructively test memory belonging to the running OS.
- [ ] **SYS-3 — Networking soak:** long DHCP/traffic run, cable/broker/server loss
  and reconnect, MQTT logging alongside storage/GUI work, and SSH session cleanup.
- [ ] **SYS-4 — Computer USB reconnect:** CDC disconnect/reconnect on final firmware
  without confusing it with host-keyboard reconnect; both consoles recover.
- [ ] **BOARD-1 — Buses/slots:** I2C/SPI/UART known-device or loopback tests;
  shared-bus locking/resource conflicts after ownership integration. Full expansion
  manifests/driver registry remain development work.
- [ ] **BOARD-2 — Final PCB:** display wiring, final power/audio rails, secondary
  display controller and intended terminal behavior. Secondary display and broader
  peripheral/Python bindings require implementation as well as later testing.

## Running and recording tests

Useful automation from the repository root (one serial owner at a time):

```sh
python3 scripts/ports/test_teensy41_hotplug.py --device sd --physical --cycles 20 --log /tmp/sd-hotplug.json
python3 scripts/ports/test_teensy41_hotplug.py --device usb --physical --cycles 20 --log /tmp/usb-hotplug.json
python3 scripts/ports/test_teensy41_hotplug.py --device keyboard --physical --cycles 20 --log /tmp/keyboard-hotplug.json
```

The keyboard runner checks reconnect/input, **not typematic timing**. Manual
KEY-2/3 remain necessary. Storage runners use unique fixtures and compare all
bytes; they do not cover active-write interruption. Removing `--physical` tests
software remount only. Clock automation sets the RTC and saves Manitoba;
read its notes before running. All host/device scripts are indexed in
[the scripts README](../../scripts/ports/README.md).

For each result, record: test ID, date, firmware hash/build, wiring, device/media,
steps, expected/actual result, PASS/FAIL/BLOCKED, log path, and memory before/after
where relevant. Preserve logs beyond `/tmp` for lasting evidence. Record partial
passes precisely; do not mark an entire group done after one happy-path check.

| Date | Test ID | Firmware/wiring | Result | Evidence / remaining issue |
| --- | --- | --- | --- | --- |
| 2026-09-29 | KEY-1 / KEY-2 partial | Keyboard legacy / AmpEn40; hash in handoff | PASS / PARTIAL | Implementation, sanitizer and dual-console checks pass; user confirmed letter/Left/Backspace/Shift repeat and release; app/multiple-key/hotplug checks remain |
| 2026-09-28 | TEL-1/2 | Telnet legacy / AmpEn40 | PASS | See Telnet device log and notes; physical cable/soak checks remain |

## Workstation commands — 2026-09-29

- [x] WORK-1: shared/port manual host checks and Clock datetime write boundary tests.
- [x] WORK-2: hardware manuals, watch ticks/exits, diagnostics, fixed sessions,
  archive byte round trips, HTTP hashes, curl cancellation/isolation and memory recovery.
- [x] WORK-3: remote man/watch/session and Telnet lifecycle regression.
- [ ] WORK-4: physical Help/Man LCD readability and navigation confirmation.
- [ ] WORK-5: make cold DF scans cooperative (51 seconds on attached media).
- [ ] WORK-6: retained sessions/fg/close, background jobs and network diagnostics
  require implementation; see the command audit rather than treating these as tests.
