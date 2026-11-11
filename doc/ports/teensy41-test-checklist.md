# Teensy / SuperKeyboard outstanding test checklist

Updated 2026-11-11. **Use this as the master test queue.** Feature notes linked
below contain procedures and historical evidence; the roadmap tracks development.
Unchecked items are outstanding, not failed unless explicitly described as such.
Do not check off a physical test based on simulation, injected input or a build.

## Folder music player

- [x] **PLAYER-1:** Actual-code host checks: folder filtering/sorting, 512-file
  limit, shuffle permutations, allocation rollback, repeat and task joining.
- [x] **PLAYER-2:** Host audio queue checks: pause, decoder stalls, stereo sample
  ordering, EOF drain and allocation/task failure cleanup; retained timer context
  and existing decoder/recording/upstream seek regressions pass.
- [x] **PLAYER-3:** Installed legacy firmware passes MP3/WAV folder playback,
  shuffle, repeat, transport/volume, corrupt-file reporting and ownership checks.
  Zero reported underruns and exact heap recovery after repeated open/close.
- [x] **PLAYER-4:** USB player/editor session switching: playback continues
  across tracks while editing and saving to SD; player next/pause/resume works;
  editor contents survive switching. Test fixtures removed.
- [ ] **PLAYER-5:** Acoustic listening with real music and broader SD/card loads;
  USB storage playback after reconnecting USB host; physical keyboard controls.
  Automated LCD/USB input and digital counters do not replace these checks.

Details and artifact locations: [player notes](teensy41-player.md).

## On-demand internal memory

- [x] **RAM-1:** Host OCRAM routing, alignment, allocation failure, DMA refusal,
  task creation failure and 100 repeated safe task-reap cycles pass.
- [x] **RAM-2:** Device Telnet start/stop, self-stop and stop during remote Python
  return OCRAM to baseline; full reconnect/authentication regression passes.
- [x] **RAM-3:** Python background/suspend/resume/cross-console/cancellation
  regression passes with unchanged memory after repeated operations.
- [x] **RAM-5:** Final firmware graceful shutdown regression passes: idle,
  blocked app, Python cleanup/refusal, serial draining and exact memory recovery.
- [x] **RAM-6:** Five foreground SSH connection failures on final firmware
  reclaim memory exactly; generated process/shutdown fixtures removed.
- [ ] **RAM-4:** Audio load regression with codec connected; current codec
  reports missing. Successful full SSH session test with a compatible test host
  key remains unverified in this memory revision; existing trust was preserved.

## On-demand audio rings

- [x] **AUDIO-RAM-1:** Actual-code host sanitizer tests pass for 100 cycles,
  allocation/configuration failures, overflow, cancellation, drain timeout,
  pointer handoff interrupts and retained/remote audio ownership guards.
- [x] **AUDIO-RAM-3:** Installed final legacy firmware: missing-codec failure
  paths, zero idle ring bytes, exact memory recovery and fixture cleanup pass.
  Free internal heap 373,180 bytes; PSRAM unchanged.
- [ ] **AUDIO-RAM-2:** Live codec capture/playback, cancel/restart, synth and
  concurrent storage/network load: one-second recording and MP3/WAV/synth
  repeat/cleanup tests now pass with codec connected. Longer SD captures overrun;
  flash capture takes excessive wall time. Acoustic tone detection failed with
  dominant 60 Hz hum. Continue load/quality checks; see newest handoff evidence.

- [x] **AUDIO-LIVE-1:** Generated MP3/WAV playback, resampling, cancellation,
  20 repeats and synth eight-voice/filter/20-restart checks pass with zero
  reported underruns and exact heap recovery. USB disconnect/reconnect also
  stops synth cleanly and the next session works.
- [x] **AUDIO-LIVE-2-SD:** On-demand 256 KiB PSRAM spool and independent feeder:
  4/30/180/300-second SD recordings pass exact length/header, wall-time and zero
  overrun checks; cancellation, overwrite protection and exact heap recovery
  pass. Peak queued audio 44,800 bytes. Host stall tests verify sample order.
- [ ] **AUDIO-LIVE-2-LOAD:** Longer-duration/card/load and acoustic continuity
  checks remain beyond the tested SD durations. USB recording is untested.
  Long flash recording is deferred by user preference; previous 30-second flash
  WAV took 46.79 seconds. Zero queue overruns alone cannot detect masked ISRs.
- [ ] **AUDIO-LIVE-3:** User audible speaker check and microphone placement/
  wiring: user confirmed a beep and MIC/GND wiring; a new RAM recording detected
  the 440 Hz tone (+38.37 dB) without overruns. Significant 60 Hz hum remains;
  speaker auto-power-off can invalidate unattended acoustic checks.
- [x] **RADIO-RECOVERY:** Installed 32 KiB WebRadio PSRAM stack correction;
  controlled HTTP MP3 playback and concurrent RAM capture work with zero reported
  underruns/overruns and 9,176 bytes of worker stack headroom. Tone capture shows
  no weak-tone gaps in 100 ms analysis windows. Speaker path user-confirmed.
- [ ] **RADIO-HTTPS:** Live Nightride MP3 stays in buffering. RTC was corrected
  from 2019 via NTP; a finite HTTPS GET on the same host returns HTTP 200, so
  streaming still needs diagnosis. Do not claim live HTTPS-radio acceptance.

## Firmware and wiring first

| Image | State | Relevant wiring / coverage |
| --- | --- | --- |
| USB-storage baseline | Historical, preserved checkpoint | Earlier physical USB/keyboard checks passed here; no SD recovery or Clock |
| Clock image | Prior validated checkpoint | Includes SD recovery and OBD demo; AmpEn is still pin 40; Clock remote checks passed |
| Workstation legacy image, stages 1–4 plus RAMFS | Prior checkpoint; sessions/jobs/processes/network diagnostics/hardware ownership device tests, UART8 loopback and basic physical repeat passed | AmpEn40, scope ADC disabled, Serial1 retained; includes scope/pdpower demos |
| Graceful-shutdown legacy image | Prior checkpoint; software shutdown and Python process regression pass | LCD CS37/reset9/WAIT15, AmpEn40, Serial1 retained; basic On/Off shutdown/wake user-confirmed; remaining power tests below |
| Normal display candidate | Built, not flashed | Telnet plus scope ADC40/AmpEn0; Serial1 UART disabled; USB/LCD retained |
| October 19 power-board image | Currently installed; PCB absent, missing-device screen and saved-limit reboot checks pass | `teensy41_power_board`; AmpEn1, DMM ADC40; GPIO5 expander INT; I2C1 SCL16/SDA17; main CS10/reset14/backlight15; small SPI0 with controls30..33 |

- [x] **SETUP-1 — Updated pin move:** user confirmed AmpEn moved to pin1;
  pin40 is the DMM input. The earlier AmpEn0/scope proposal is superseded.
  Serial1 is disabled in the module and power-board profiles. GPIO5 is now the
  MCP23008 interrupt on the power PCB, not a direct USB host enable output.
- [ ] **SETUP-2 — Final-PCB boot regression:** after connecting the revised
  power board, verify LCD/USB shells, keyboard/powered hub, `/sd`, `/usb`,
  `/flash`, Ethernet, Wi-Fi and app listing. Check physical pin ownership and
  rail sequencing; host tests and the PCB-absent boot do not establish these.

Earlier SD/Clock/OBD evidence refers to the corresponding historical images.
Keep older checkpoint files with their wiring descriptions.

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
- [x] **CLOCK-BOOT — Saved-timezone restoration (2026-11-11):** reproduced UTC
  after reboot while the checksummed flash file retained Manitoba/UTC5. Fixed
  premature caching of failed reads, serialized timezone settings access, and
  restored settings before background consumers start. Host tests pass delayed
  flash mounting, temporary namespace contention, save rollback and reload.
  Flashed the fix and verified the saved timezone and local clock across three
  software reboots without issuing a timezone-setting command. The flash file's
  SHA256 remained `3fd6bba73c2a088c7945bc8c4ca165ff2e325caa9a35d6ba6b8db9763faf23e2`.
  Capture: `/tmp/solaros-timezone-fixed.json`; upload: `/tmp/solaros-timezone-upload.log`.
  Installed HEX SHA256:
  `a4293edba5f884b7a246f6d1b6056823566de01a182c5302bbd7612a4405649c`.
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
- [ ] WORK-6: network diagnostics remain pending; background jobs pass host/device validation;
  retained app sessions/fg/close are implemented (SESS checks below).

## Retained app sessions — 2026-09-30

- [x] SESS-1: host sanitizer lifecycle, four-slot limit, nested chains, ownership,
  busy-owner rejection, stale IDs, allocation/start failures and 1,000 cycles.
- [x] SESS-2: USB/LCD device retention, calculator input, editor save after resume,
  memory recovery, cross-console ownership and USB disconnect isolation.
- [x] SESS-3: graphical Plot suspend/text switch/resume and continuing frame output.
- [x] SESS-4: Telnet retention/resume and disconnect cleanup on final image.
- [x] SESS-5: user confirmed physical Ctrl+Z, calculator input retention and Plot redraw.
  Editor save/state has automated coverage; broader editor visual checks remain follow-up.

Dynamic shell creation, arbitrary migration and session create/send/focus remain
unimplemented. See [session notes](teensy41-sessions.md).

## Background jobs and schedules

- [x] JOBS-1: sanitizer tests for four slots, cooperative waits/stops, bounded output,
  rejection, file/allocation errors and 1000 cleanup cycles.
- [x] JOBS-2: shared scheduler worker/queue modes, save/reload and rejected queue;
  Teensy local/UTC conversion, DST gap and RTC upper boundary tests.
- [x] JOBS-3: USB/LCD responsiveness, isolated cwd/output, disconnect survival,
  repeated-job memory recovery and device error paths.
- [x] JOBS-4: shared Clock countdown while retained, schedule trigger/skip/manual
  run and disabled entry persistence across reboot.
- [x] JOBS-5: recovered startup RAM-bank regression; link-time RAM1 headroom guard.

## Detachable Python processes (stage 2b)

- [x] PROC-1: safe Ctrl+Z/bg/fg, one VM admission, continued file logging beside
  Calc, tail inspection and shared IDs across USB/LCD reattachment.
- [x] PROC-2: partial input()/REPL preservation, no detached shell-key consumption,
  bounded output and completed-job reattachment beside an archive worker.
- [x] PROC-3: cooperative stop from running/suspended states, file closure,
  eight repeated CPU-loop stops with exact warm heap/PSRAM recovery.
- [x] PROC-4: detached USB disconnect survival; foreground disconnect cancellation
  and cleanup; foreground graphics remain functional and cannot detach.
- [x] PROC-5: Python network regression: DNS/TCP, exact file download, timeout,
  Ctrl+C, GC/socket cleanup and restart. Tail newline/empty-file boundaries pass.
- [ ] PROC-6: user physical-key confirmation of Ctrl+Z/bg/fg for a Python logger;
  firmware key-injection and USB control-byte acceptance already pass.

## Network diagnostics and time synchronization (stage 3)

See [feature notes](teensy41-network-diagnostics.md). Final-image evidence:
`/tmp/teensy-netdiag-device.json`; recovery snapshot `2026-09-30-netdiag`.

- [x] **NETDIAG-1 — Protocol bounds:** ASan/UBSan target/port parser checks, NTP
  malformed/source-token rules, unsynchronized/denied replies, timestamp ordering,
  2036 rollover and 2106 RTC bound. Source endpoint checks also run on-device.
- [x] **NETDIAG-2 — Local interoperability:** ICMP replies/loss/RTT, TCP open/closed
  ports and /32 targets, NTP query-only, actual UTC synchronization, bad replies,
  denial and rollover. Six embedded-manual tests pass.
- [x] **NETDIAG-3 — Cancellation and ownership:** Ctrl+C for all three commands,
  concurrent LCD/USB diagnostics, link down/up recovery; five repeated cycles
  recover exactly 29,124 internal / 8,163,984 PSRAM bytes free.

No public-server availability, service fingerprinting, authenticated time or
long-term clock-discipline claim is implied by these local tests.

## Shared Tab completion

See [API and limits](teensy41-completion.md). Final device evidence:
`/tmp/teensy-completion-device.json`; checkpoint `2026-09-30-completion`.

- [x] **COMPLETE-1 — Shared engine:** ASan/UBSan checks for cursor token replacement,
  later-argument preservation, quoted/escaped spaces, raw fields, directory-only
  filtering, longest common prefix, repeat Tab, capacity/error atomicity and
  bounded listings. Shared TUI widget and existing shell completion tests pass.
- [x] **COMPLETE-2 — Console integration:** USB command/path/settings completion,
  earlier source argument in `cp`, retained Calc `fg`/`close` IDs, detached Python
  `job status` ID, and LCD second-Tab listing/redraw all pass.
- [x] **COMPLETE-3 — Cleanup:** five cycles recover exactly 29,188 internal
  / 8,163,984 PSRAM bytes free. Test SD fixtures remain for inspection.
- [ ] **COMPLETE-4 — Physical keyboard confirmation:** try `ed<Tab>`, a path with
  spaces, and a second Tab on an ambiguous directory at the local keyboard.
  USB/LCD injection is verified; actual keypresses are optional user follow-up.

## Hardware resources and COM (stage 4)

See [routing, commands and limits](teensy41-hardware-resources.md). Evidence:
`/tmp/teensy-hardware-device.json`; checkpoint `2026-09-30-hardware`.

- [x] **HW-1 — Host ownership:** ASan/UBSan, atomic UART rollback, board pin
  protection, wrong-owner access, partial/blocked TX and repeated cleanup.
- [x] **HW-2 — Device lifecycle:** USB/LCD ownership exclusion, retained COM,
  disconnect cleanup, completion and exact memory recovery over five cycles.
- [x] **HW-3 — Physical UART8:** jumper RX34/TX35 raw read/write and COM echo.
  Port closed afterward; user told jumper can be removed.
- [ ] **HW-4 — External peripherals:** I2C read/write against a known device and
  SPI mode/rate/data validation against a known peripheral; observe bus signals.
- [ ] **HW-5 — Wider UART coverage:** physical UART7, sustained RX throughput,
  overflow behavior and another serial endpoint. UART3 conflicts with display WAIT.

## PSRAM RAMFS (original item 7)

See [RAMFS notes](teensy41-ramfs.md). Final main acceptance:
`/tmp/teensy-ramfs-device.json`; additional app/reboot checks:
`/tmp/teensy-ramfs-edges.json`; checkpoint `2026-10-01-ramfs`.

- [x] **RAMFS-1 — Backend:** sanitizer tests for quotas, sparse seek/append,
  busy handles, invalid mounts, rename cycles, descriptor exhaustion and cleanup.
- [x] **RAMFS-2 — Device routing:** Python, shell, Files, archives, SD copy/move,
  completion, df, suspended/background worker busy unmount and ENOSPC integrity.
- [x] **RAMFS-3 — Recovery:** five cycles recover exactly 28,516 internal /
  8,158,704 PSRAM bytes free after unmount.
- [x] **RAMFS-4 — App and reboot checks:** editor create/overwrite save, LCD
  visibility, four mounts, flash transfer and actual reboot volatility.
- [ ] **RAMFS-5 — Long-running workload:** sustained background logging with
  concurrent app use, varying quotas and fragmentation under realistic data rates.

## Python syntax highlighting

- [x] **SYNTAX-1 — Lexer/cache:** sanitizer token tests, randomized incremental
  equivalence, bounded propagation and large-file convergence.
- [x] **SYNTAX-2 — Device:** all colors, selection contrast, triple-quote edits,
  resume, shell color restoration, LCD text and exact five-cycle cleanup.
- [ ] **SYNTAX-3 — Physical readability:** inspect Python colors and selection
  on the actual LCD; try editing and scrolling a representative large script.

See [syntax notes](teensy41-syntax.md) and `/tmp/teensy-syntax-device.json`.


## Upstream 4.15.18 integration: MIDI, WebRadio and USB-PD

These physical checks remain pending even when automated software checks pass.

- [ ] **MIDI-1 — USB input/output:** enumerate a class-compliant USB MIDI device
  on the powered host hub with keyboard/storage present. Record note, controller,
  pitch-bend, program-change and real-time messages; verify channels and timing.
  First implementation supports USB cable 0 and short messages; SysEx is excluded.
- [ ] **MIDI-2 — DIN/UART:** fit a proper 3.3 V compatible MIDI input/output
  interface (including isolated MIDI input). Verify 31250 baud and slot pin
  routing. Record on USB and replay through UART; then test the reverse.
- [ ] **MIDI-3 — Load/recovery:** sustain dense events while SD writes and other
  consoles run; measure timestamp jitter. Confirm overflow reporting, disconnect
  handling, Ctrl+C cleanup and receiver note release. SMR1 files are timestamped
  event recordings, not Standard MIDI Files; viewer/editor and .mid import/export
  remain future work. USB device-mode MIDI is not enabled.
- [ ] **RADIO-1 — Audio hardware:** reconnect SGTL5000, verify real MP3 radio
  playback, pause/resume, volume and ownership against Synth/aplay/recording.
- [ ] **RADIO-2 — Network/audio soak:** test HTTP and HTTPS stations, reconnects,
  stalls and cancellation while audio plays; check underruns, PSRAM recovery and
  external worker stack headroom. Teensy defaults to the text UI; PCM buffer is
  bounded at 128 KiB. Lack of an audio shield must produce a clean error.
- [ ] **PD-1 — Wiring/ratings:** STUSB4500 is NOT wired on the current bench.
  Record the I2C bus, address straps, ALERT wiring, board voltage/current ratings,
  regulator limits and load-switch arrangement before `pd open`. No auto-start.
- [ ] **PD-2 — Controller event timing:** measure alert-to-PDO capture latency
  under load; source capability registers can be overwritten in about 3 ms.
  The polling backend must be validated with actual hardware; add interrupt
  notification/dedicated bus speed as required. Missed/stale messages must never
  become a confirmed contract. Check fixed source PDO position mapping.
- [ ] **PD-3 — Negotiation:** explicit open starts 5 V discovery. Verify source
  capabilities, limits, successful requests, reject/wait, mismatched contract,
  timeout, detach/reconnect, hard reset and I2C errors. Measure VBUS externally;
  displayed contract current is not a current measurement.
- [ ] **PD-4 — Exit/reset behavior:** `pd close` stops monitoring and releases the
  address; it DOES NOT switch off power or restore 5 V. Controller state persists
  independently of app exit. Verify power-cycle defaults and confirm no NVM
  writes or high-voltage restore on firmware boot. No load/motor control added.
- [ ] **BUS-5 — New clients:** verify MIDI UART and PD I2C ownership blocks raw
  commands from accessing claimed resources; retest external I2C/SPI peripherals.

- [ ] **UPSTREAM-NTP:** rerun `test_teensy41_compose.py` host UDP NTP fixture.
  On the 4.15.18 integration, command/pipe and PSRAM pressure checks passed, but
  the local NTP reply timed out; network regression remains unverified.

## Python hardware and offline library bundle

- [ ] **PY-HW-I2C:** real sensor on each exposed bus: 100 kHz, 8/16-bit register
  addressing, repeated START, 32-byte limits, device removal/timeouts and lease
  conflicts. LSM9DS1/BMM150 sources are bundled; actual measurements unverified.
- [ ] **PY-HW-SPI:** expansion slot1 modes 0..3 (slot0 CS37 is reserved by the bench LCD), automatic CS, full duplex,
  clocks through 12 MHz, disconnect and cancellation; drivers requiring held CS
  over multiple calls need an explicit future API.
- [ ] **PY-HW-UART:** Serial7/8 cross-port loopback, partial reads/writes, timeouts
  and dense input under load. Serial3 remains blocked by legacy LCD wiring.
- [ ] **PY-HW-ADC:** validate scaling and calibration using safe known voltages
  on an unreserved analog pin (pin14 on this bench). read_u16 scales the native
  default 10-bit conversion.
- [ ] **PY-HW-PWM:** measure frequency/duty and deinit state on 28/29 or 36/37;
  verify paired timer ownership excludes other clients. No waveform test yet.
- [ ] **PY-LIB-EXTRA:** packages in the full micropython-lib SD archive are source
  availability only. Validate dependencies and missing native modules before
  promoting any of them to the installed /flash/lib set.

### Serial terminal/logger follow-up

Host ASan/UBSan tests pass for ring overflow/wrap, binary fidelity, independent
ports, framing validation, XON/XOFF, partial writes and failure cleanup. Device
acceptance passes for exact TX logs in CR/LF/CRLF modes, configuration rejection
while active, COM suspend/close, console disconnect, no-overwrite and repeated
memory recovery (36,100 internal / 8,123,928 PSRAM bytes free).

- [ ] **SERIAL-RX — External source:** verify binary RX including 00/11/13/ff,
  text/hex rendering, CR/LF/CRLF TX and long capture against a sender checksum.
- [ ] **SERIAL-FRAME — Framing:** external peer/logic analyzer verification of
  7E1/7E2/7O1/7O2 and 8N1/8N2/8E1/8E2/8O1/8O2 at representative baud rates.
- [ ] **SERIAL-FLOW — XON/XOFF:** peer pause/resume in both directions, already
  queued TX, log pressure, terminal suspension, stop and error recovery.
- [ ] **SERIAL-LOAD — Sustained capture:** two UARTs at high baud with SD/USB,
  network/audio load and PSRAM pressure. Validate dropped-byte accounting and
  check capture/writer stack high-water marks. Hardware overruns are unmeasured.
- [ ] **SERIAL-STORAGE — Failure:** full/removed SD, long storage stalls and
  power interruption. Confirm visible error, stop cleanup and filesystem recovery.
- [ ] **SERIAL-CTS — Future implementation/hardware:** use the approved final-PCB
  CTS header 7 / GPIO37 and RTS header 11 / GPIO25; DTR moves to header 12 /
  GPIO24. Preserve bench GPIO37 LCD CS. Resolve XBAR CTS polarity and shared
  I2C2/CS ownership, wire an external peer, implement bounded shutdown with CTS held
  inactive, then verify hardware backpressure. RTS/CTS is not enabled in this version.

## Graceful On/Off shutdown — 2026-10-04

See [shutdown behavior and wiring](teensy41-shutdown.md). Use the legacy bench
profile; no GPIO reassignment is needed for the dedicated On/Off pad.

- [x] **OFF-SW — Software acceptance:** coordinator/serial/PD host sanitizer
  tests and final-device `--check` acceptance pass, including Python finally,
  refusal timeout, foreground REPL, script/serial cleanup and memory recovery.
  HEX `feb87ed27c696eaaad771b8091a46489df77b619225a0f1edb050018c5b0b48a`.
- [x] **OFF-1 — Physical tap/wake:** user confirmed on 2026-10-04 that briefly
  jumpering On/Off to GND shut down the Teensy, and a second jumper action woke
  it again, on the installed graceful-shutdown firmware. User-reported result;
  electrical timing and rails were not measured.
- [ ] **OFF-1B — Peripheral recovery:** explicitly verify LCD, USB keyboard,
  mounted storage and both shells after an On/Off shutdown/wake cycle.
- [ ] **OFF-2 — Button during Python/logging:** repeat with a cooperative Python
  file writer and a serial/MIDI recording. Inspect closed files after wake;
  compare with `poweroff --check` software acceptance. Repeat with a mounted
  USB storage volume; the current device test covered SD and flash, not USB.
- [ ] **OFF-3 — Refusal and emergency hold:** open an unsaved editor and verify
  a tap leaves power on with a blocked status. With only disposable test files,
  verify the hardware long hold still cuts power; this bypasses cleanup.
- [ ] **OFF-4 — USB-PD:** wire STUSB4500, open with verified board ratings,
  negotiate a supported high voltage, then measure confirmed 5 V before CPU
  off. Test reject/timeout/detach and closed-monitor refusal. Check the 5 V
  regulator remains adequate for wake when USB VBUS itself is 5 V.
- [ ] **OFF-5 — External rails:** implement/validate regulator EN control in the
  final PCB. The current external 3.3 V regulator EN is tied to +5 V; CPU TOP
  does not shut down that rail or the separate LCD/USB loads. Validate backfeed,
  I2C pullups and independent 5 V fallback for forced cutoff/reset.

## Saved keyboard Num Lock — 2026-10-04

- [x] **KEY-NUM-SW — Preference persistence:** on/off and invalid argument
  behavior, reboot reload, driver Num Lock state and unchanged free memory
  verified on the installed legacy image; keyboard/settings host tests pass.
- [x] **KEY-NUM — Physical reconnect:** after deferring the LED request until
  SET_IDLE completes, user confirmed the light, manual toggle, unplug/replug
  auto-enable, letters and keypad digits all work on the bench keyboard.
- [ ] **KEY-NUM-OTHER — Additional keyboards:** repeat the LED/manual toggle/
  reconnect/keypad check with another keyboard when available.

## Flash shell history — 2026-10-04

- [x] **HIST-HOST:** ASan/UBSan: batching, save/open/close/replace failures,
  retained old file, bounded load, incomplete records, console isolation and
  timer wrap.
- [x] **HIST-DEVICE:** timed LCD/USB flash saves, separate histories, USB/LCD
  Up-arrow recall after reboot, and `poweroff --check` save complete.
- [ ] **HIST-TELNET:** verify recall on a fresh remote connection on hardware;
  the shared persistence implementation and separate path pass host tests.
- [x] **NET-BOOT:** saved `network up` in `/flash/.shell/startup` and selected
  `setterm startup flash`; post-reboot status reports DHCP bound without issuing
  a network start command manually.

## SSH configuration/entropy repair — 2026-10-05

- [x] **SSH-CONFIG:** client config uses `/flash/.ssh`, matching sshkey.
- [x] **SSH-ENTROPY-HOST:** ASan/UBSan completed-sample preservation, error
  recovery, request bounds and restart after peripheral clock disable.
- [x] **SSH-REGRESSION:** isolated-server password login, bulk/bidirectional I/O,
  editing keys, wrong-password and changed-host-key rejection, cancellation,
  remote close and repeated cleanup pass. Warm memory is unchanged across repeats.

## FAT32 USB usage batching — 2026-10-05

- [x] Batch helper sanitizer tests: bounded transfers, exact data/order, tail,
  read/callback failure and sector-address overflow.
- [x] Installed legacy image: cold USB scan 45.828 → 5.791 seconds; cached
  `df` 0.031 seconds; `df --refresh` about 5.82 seconds on current FAT32 drive.
- [x] Cached counts agree with recounts after create/truncate/rename/delete;
  hashes pass through PSRAM reads; refresh refuses live USB handles; logical
  eject/remount agrees; generated fixture removed, original usage restored.

Evidence: `/tmp/teensy-df-device.json`. These checks do not establish physical
removal during active I/O or compatibility with other drive formats/controllers.

## Complete offline manual — 2026-10-05

- [x] **MAN-1 — Registry and rendering coverage:** all 27 apps and 67 commands
  have pages. Full/reduced profile gates match actual registries. Every body is
  embedded; default upstream download policy remains unchanged. Fifteen Teensy
  and sixteen shared generator tests pass.
- [x] **MAN-2 — Hardware audit/build:** Ethernet, flash paths, SGTL5000 capture,
  single-core ltop, terminal Synth/Calc/WebRadio, LCD appearance, and pending
  scope/PD/OBD hardware are described accurately. Legacy firmware builds.
- [x] **MAN-3 — Device acceptance:** installed legacy build; all 94 pages pass
  beginning/end text checks, bare-name lookup, search, browser and LCD paging.
  Free internal/PSRAM memory is identical before/after the run. Evidence:
  `/tmp/teensy-manual-device.json`. Index summary truncation and backward wrapped
  row traversal bugs discovered during acceptance are fixed; the new navigation
  regression also passes. See handoff for the installed image hash.

## Motherboard power path — 2026-10-19

Details: [power-path wiring, commands and shunt selection](teensy41-power-path.md).
The PCB is **not connected**. The user confirms that the INA237's dedicated
3.3V regulator is upstream of the LTC4417 MOSFETs. Thus switching system power
off should leave the INA powered while an input remains; this must be measured.
The user is still adding the LTC4417 UV/OV divider resistors. These prerequisites
and the shunt footprint/pad mapping remain outstanding, not firmware test passes.

- [x] **PWR-SW — Host acceptance:** actual driver/formatter sanitizer tests,
  missing/wrong devices, nominal 10mOhm conversion, threshold readback/failure,
  signed current/power, capture/fault latch, boot-OFF/reset recovery, open-drain
  KILL, passive PD and row bounds pass. Shutdown tests cover successful cleanup,
  refused/failed cleanup, failed KILL and `--check` without asserting KILL.
  Pin reservation tests cover legacy/display/module/power profiles.
- [x] **PWR-ABSENT — PCB-absent device acceptance:** power-board image flashed;
  Power page reports Meter/Inputs/PD/USB host unconnected, no fabricated zeros,
  zero SPI misses. Strict max parsing and saved pending limit survive an
  acknowledged reboot. Test limit restored to the original off setting.
- [ ] **PWR-1 — Assembly prerequisites:** complete U4 UV/OV resistor networks;
  verify intended voltage windows and component ratings. Replace R13's humidity
  sensor footprint with the chosen 10mOhm Kelvin shunt footprint; check force
  and sense pad mapping against the BOM and manufacturer drawing. Verify ground
  address straps: U5 MCP23008=0x20, U13 INA237=0x40, U3 STUSB4500=0x28 on I2C1.
- [ ] **PWR-2 — Always-on supply and pullups:** measure the INA237 3.3V supply
  before and after LTC4417 shutdown and during Teensy reset/hang. Check GP6
  ALERT/KILL remains open-drain, MCU/expander rail-off leakage/backfeed and I2C
  levels. Confirm the INA is not inadvertently reset when switched rails drop.
- [ ] **PWR-3 — LTC4417 validity/priority:** test each input alone, combinations,
  unplug/replug, out-of-window and boundary voltages. Compare VALID1/2/3 against
  GP0/1/2 and the Power page. Measure actual path selection/switchover separately
  from inferred PD > barrel DC > AUX priority; check no reverse current.
- [ ] **PWR-4 — INA237 accuracy:** use a reference meter/load to compare bus V,
  signed current and W at low current and several loads through 5A within board
  ratings. Measure shunt temperature/drop and actual resistance; check current
  direction, zero offset, Kelvin routing and data invalidation on I2C failure.
- [ ] **PWR-5 — Independent trip with Teensy halted:** first configure and read
  back a conservative threshold, then deliberately halt/reset the Teensy or
  prevent its boot while keeping INA standby power present. Increase a bounded
  test load and scope shunt voltage, ALERT/KILL, LTC2954 EN and switched VOUT.
  Verify power cuts without any Teensy instruction or expander transaction.
  Repeat after a system shutdown/wake with the upstream INA supply retained;
  confirm threshold retention and measure response/overshoot.
- [ ] **PWR-6 — Complete input loss/cold start:** remove all INA supply, restore
  input with Teensy held reset, and measure the default behavior. INA registers
  are volatile: distinguish retained standby operation from a truly cold start.
  Verify the interval before firmware restores the saved threshold. A custom
  limit immediately after total power loss requires additional independent
  configuration/protection hardware; do not mark this requirement met by NVS.
- [ ] **PWR-7 — LTC2954 graceful button sequence:** scope physical PB, INT_N,
  MCP23008 INT/Teensy5, GP6/KILL_N, EN and switched VOUT. A tap must request
  SolarOS cleanup, then KILL only after applications/jobs stop, logs drain and
  storage sync succeeds. Run with Python, FTP, serial logging and mounted SD/
  USB storage using disposable fixtures. Verify files after restart. Confirm
  `poweroff --check` leaves GP6 as input; repeat with failed/blocked cleanup and
  I2C failure. Measure the C71 forced-hold deadline versus cleanup duration.
- [ ] **PWR-8 — Forced button cutoff:** with the Teensy held reset/halted, verify
  LTC2954's long hold independently removes switched power. Check release/wake
  behavior, retained INA supply and that software never drives shared KILL high.
- [ ] **PWR-9 — TPS25820 host:** verify GP3 EN OFF at boot, explicit ON, GP5
  SINK attachment, device/keyboard/storage enumeration, GP4 fault assertion and
  latched OFF. Check clear never enables, active fault refuses clear/ON, short
  pulses/capture limitations, reset/reconnect remains OFF, and explicit OFF
  refuses mounted USB storage. Measure host VBUS and hardware current limiting;
  EN/SINK alone do not prove voltage. Confirm CHG grounded/default advertisement.
- [ ] **PWR-10 — STUSB4500 PD status:** compare requested VSEL target/RDO current
  with a PD analyzer/source at supported voltages. Check waiting/mismatch,
  detach/reconnect and I2C errors. Verify passive reads consume no negotiation
  events, active `pd` ownership remains exclusive, no spontaneous higher-voltage
  request/NVM writes occur, and RDO current is never called measured consumption.
- [ ] **PWR-11 — Concurrent load and display:** repeat telemetry/button/USB/PD
  checks with Python, FTP, Wi-Fi and DMM active; stop DMM to inspect the Power
  page. Verify NES navigation, no full-screen flashing, no stale valid readings,
  bounded I2C latency, task stack margins and storage integrity after shutdown.

## IMU, magnetometer, level and compass — 2026-11-02

Motherboard `Environment.kicad_sch`: U17 LSM6DSOX at I2C1 `0x6a` (SA0
grounded), U19 MMC5983MA at `0x30`, SDA17/SCL16. IMU INT1 is GPIO22 and
Mag INT is GPIO38; the current driver polls and leaves these GPIOs untouched.
Enabled by `SK_MOTION=1` in `teensy41_power_board`.
Details: [sensor pages and controls](teensy41-display.md#digital-level-and-compass).

- [x] **MOTION-SHARED-SW — Shared services (2026-11-10):** IMU stream registry
  and magnetometer registry/stream tests cover simultaneous handles, unregister
  protection, duplicate-stream rollback, byte layout, CSV and capability masking.
  The production Teensy provider host test verifies SI conversions, stable sample
  timestamps, stream reads, cache-only consumers, independent sensor recovery,
  expiry and bounded waits. The level/compass tests use shared service reads.
- [x] **MOTION-SHARED-ABSENT — Installed shared services (2026-11-11):** flashed
  the `teensy41_power_board` image built from `59a27f1b`. `imu list`, `mag list`
  and stream metadata confirm `imu0`/LSM6DSOX, `mag0`/MMC5983MA, `imu.imu0`
  (`imu-v1-le`) and `mag.mag0` (`mag-v1-le`, uT). Direct and stream samples
  report `NOT_FOUND`. Four repeated failed reads per stream leave zero handles.
  With the dashboard disabled, the motion task remains running with 2848 bytes
  of stack headroom. Level and Compass report missing hardware across repeated
  probes, with advancing updates/scans, zero SPI misses and NES faults, and
  1540 bytes of monitor stack headroom. Returned to System with USB responsive.
  The separate paired-controller check also passed six observations, with scans
  advancing from 6987 to 7564 and zero reader faults.
  The acceptance script now accepts Teensy's `NOT_FOUND` label as well as the
  ESP-style error name. Capture: `/tmp/solaros-shared-motion-absent-20261111.json`;
  upload: `/tmp/solaros-shared-motion-upload-20261111.log`. Installed HEX SHA256:
  `7e808c3a33a428d4d3e4933579a3287537279f29eedc25f4db3abc5194807373`.
- [ ] **MOTION-SHARED-DEVICE — Assembled sensor acceptance:** compare direct and binary/CSV
  samples, their timestamps, units and valid bits while changing dashboard pages
  and running an app that owns the display. Confirm polling and recovery continue.

- [x] **MOTION-SW — Host acceptance:**
  `bash scripts/ports/test_teensy41_motion_host.sh` passes for enabled/disabled
  sensor builds: identity/configuration, signed IMU and packed 18-bit magnetic
  decoding/scales, independent I2C failures/retry, data-ready expiry and timestamp
  rollover, level angles/invalid acceleration, cardinal headings/field rejection,
  calibration coverage/previous-fit preservation, zero/reset/cancel, missing
  sensors and graphics bounds/stale-pointer clearing. Address/undefined-behavior
  sanitizers pass (leak detection disabled under the sandbox's ptrace).
  Small-page and dual-input host tests pass for seven-page wrap, either-source
  sensor actions, edge-only dispatch and display ownership suppression.
- [x] **MOTION-ABSENT — Installed image, sensors absent:** flashed the power-board
  image, run `python3 scripts/ports/test_teensy41_motion_absent.py --log LOG.json`.
  Level must report `IMU missing / stale`; Compass must report
  `Magnetometer missing/stale`, without fabricated readings. Observe several
  two-second probe retries, advancing display updates/NES scans, no SPI/reader
  faults, sufficient monitor stack headroom, responsive USB and return to System.
  Passed 2026-11-02: three observations per page across repeated probes;
  Level updates 49→94 / scans 2002→2472, Compass updates 118→163 /
  scans 2726→3196, zero SPI misses and NES faults, monitor stack headroom
  1476 bytes. USB status text confirmed both missing-device messages; physical
  screen appearance and real-sensor accuracy remain untested. Returned to System.
  Capture: `/tmp/solaros-motion-absent.json`; upload:
  `/tmp/solaros-motion-upload.log`. Installed HEX SHA256:
  `7082fbcd80021dc02f756e1f7774527609ab3bcba1c4e018135dee6e2144f365`.
- [ ] **IMU-1 — Assembly/identity:** verify U17 supply, decoupling, CS high, SA0
  low and shared SDA17/SCL16 pullups/levels. Check address `0x6a`, WHO_AM_I
  `0x6c`, register readback and data-ready operation. Confirm GPIO22/38 remain
  unclaimed by this polling driver; inspect other consumers before using IRQs.
- [ ] **IMU-2 — Axis and scale:** verify PCB/case axis directions on all six
  faces (+/-1 g), approximately zero stationary angular rate and signed response
  to known rotations. Compare pitch/roll to a reference level at 0, +/-5, +/-30
  and near +/-90 degrees; check inverted orientation and angle wrapping.
- [ ] **LEVEL-1 — Display/controls:** check bubble direction, range clipping,
  green within 0.5 degrees, A relative zero/B absolute reset, either NES input,
  seven-page navigation and no actions leaking through app/console ownership.
  Moving/free-fall/high-acceleration input must not be shown as a valid level.
- [ ] **MAG-1 — Assembly/identity:** verify U19 supply/CAP decoupling, CS high,
  I2C `0x30`, product ID `0x30`, 18-bit centered decode, data-ready clear and
  continuous 20 Hz automatic set/reset. Confirm field units and signed axes.
- [ ] **MAG-2 — Calibration:** rotate the assembled device through all axes,
  A start/A save, sufficient sample count and spans, inadequate-coverage refusal,
  B cancel preserving an earlier calibration, and restart behavior (fit is RAM
  only). Check hard-iron/diagonal scale correction against raw values. Test with
  the final enclosure, speakers and normal operating currents.
- [ ] **COMPASS-1 — Direction/reference:** verify package +X reference against
  the physical forward edge and heading sense. Compare N/E/S/W and 0/360 wrap
  with a reference magnetic compass, away from interference. Confirm the north
  pointer, eight-point label and calibration indication. Document case mounting
  correction if needed; do not claim true north or tilt compensation.
- [ ] **COMPASS-2 — Invalid conditions:** tilt beyond approximately 15 degrees,
  move the device, apply/remove magnetic interference and approach vertical field.
  Confirm hold-flat/invalid-field messages and removal of stale graphics/readings.
- [ ] **MOTION-RECOVERY — Independent failures/load:** with power removed before
  rewiring, test each sensor absent alone, both absent and reconnection/restart.
  Confirm the healthy source still updates, data older than 500 ms is hidden,
  two-second recovery works, and shared Wire1 devices remain usable. Repeat with
  audio/network/storage load; inspect update cadence, stack margins and SPI/reader
  faults. App/console ownership must suppress sensor rendering and restore pages.

## Two physical controller inputs — 2026-10-26

GPIO4 reads the on-board U12 4021 buttons; GPIO6 (U1 symbol pin8) reads the NES
connector directly. Clock2 and latch3 are shared. GPIO8 remains codec I2S input.
Details: [dual input, APIs and timing](teensy41-display.md#two-physical-controller-inputs--2026-10-26).

- [x] **NES2-SW — Host acceptance:** paired capture/DMA lifecycles and coherent
  publication, matched-clock validation, either-side errors/timeouts, GPIO
  fallback reading both streams, shutdown isolation and IRQ preservation.
  Independent debounce, all65,536 button-state pairs, source-specific and atomic
  pair APIs, either-source navigation/Music, merged Game Boy/keyboard controls,
  ownership transitions and GPIO6 reservation with codec GPIO8 preserved pass.
- [x] **NES2-READ — Live reader acceptance:** updated image flashed and USB
  status checked across six observations. Mode `flexio-dual-dma`, two separate
  masks, 576 completed paired scans during the observation window, zero reader
  faults and 980 bytes of monitor stack headroom. Both masks were idle00;
  this does not establish physical button wiring or capture phase. Image SHA256:
  `762067e574d9c315109b71c7ae687f455ede6ddee8199683a43d03e03f47ab40`.
- [ ] **NES2-1 — Electrical/timing:** connect both inputs as drawn, probe shared
  latch/clock plus data4/data6, and confirm bit order/settling and FlexIO1/2 sample
  phase at startup and under audio/SPI/storage load. Verify100Hz scans, no extra
  connector clock/latch outputs, and no reader faults/fallback in normal use.
- [ ] **NES2-2 — Physical independence:** exercise all eight buttons separately
  on each source; `lcd small` must show the correct independent mask. Hold a
  button on both, release one, and verify the other stays held. Repeat different
  simultaneous combinations, bounce and unplug/replug of just the external pad.
- [ ] **NES2-3 — Navigation/apps:** navigate dashboard pages, Music transport,
  DMM hold/range/history and single-player Game Boy with either source and both.
  Check keyboard coexistence, no phantom navigation after apps close, and no
  stuck buttons during source removal. Separately verify codec capture/playback
  still works on GPIO8 and DMA/CPU latency under load remains acceptable.

### DNP3 lab (power-board build)

- [ ] Run `bash scripts/ports/test_teensy41_dnp3_host.sh`: independent CRC oracle,
  transparent forwarding/partial writes, transactional rewrite fallback, packed
  binary and floating analog edits, directional faults, malformed objects,
  transport-fragmented class-0 reads, SBO/direct controls, expired selects,
  duplicate requests/FCB suppression, profiles and background log cleanup.
- [ ] Run `python3 scripts/ports/test_teensy41_dnp3.py --log /tmp/dnp3-live.json`
  with idle LCD, exclusive USB, SD and Ethernet. This uses only host-owned test
  connections and a unique SD fixture; it leaves the internal demo visible.
- [ ] Physical serial pass-through: connect known bench endpoints through UART7
  and UART8 with appropriate transceivers; test both directions, mismatched baud
  settings, supported parity/stop-bit modes, sustained throughput/backpressure,
  corrupt frames and reconnect. Verify UART leases reject occupied wireless slots.
- [ ] Check RS-232/RS-485 electrical levels and wiring; RS-485 automatic DE/RE
  control is not implemented. GPIO UART pins are 3.3V only.
- [ ] Interoperate master/outstation simulation with an independent DNP3 stack
  over real serial hardware. Verify class-0 reads, link RESET/STATUS, confirmed
  link retransmission, application confirmations and successful/rejected controls.
- [ ] Verify live control-address/status edits on supported single-frame objects,
  original payload retained on fragmented/unknown/authenticated messages,
  directional fault counters and received-versus-transmitted audit records.
- [ ] Exercise SD full/write failure and long capture with explicit log_lost
  counts, concurrent Python/FTP, retained sessions, USB disconnect and app closure.
  Simulator is a documented subset; secure authentication, event queues,
  application fragmentation and time synchronization are not supported.

### Reader and Writer

- [ ] Run `bash scripts/ports/test_teensy41_documents_host.sh` with sanitizers:
  Markdown/source layout, UTF-8 buffer/undo, save rollback failures, deflated EPUB
  chapters, relative PNG asset lookup and bounds/cleanup.
- [ ] Run `python3 scripts/ports/test_teensy41_documents.py --log /tmp/documents-live.json`
  with idle LCD, exclusive USB, SD and Ethernet. Uses unique SD fixture documents
  and a temporary local FTP service; closes FTP and cleans its document metadata.
- [ ] Main LCD: text/Markdown/EPUB layout, embedded images, find, zoom, row/page
  navigation, saved position/chapter, suspend/resume and Files -> Reader return.
- [ ] Writer: typing/selection, clipboard, formatting, find/replace, synced save,
  Ctrl+Z undo/Ctrl+Y redo and F12 suspend. Confirm other apps still suspend with
  Ctrl+Z. Verify Recovery and Save/Discard/Cancel prompts with physical keyboard.
- [ ] Exercise reader and writer retained together, concurrent background apps,
  repeated open/close memory recovery, missing/corrupt documents, >256 KiB Writer
  rejection, SD removal/full-volume save failures and reopening recovery snapshots.
- [ ] Read long real-world EPUBs with varied images and typography. WebP, DRM and
  browser CSS are not supported on this backend. GUI apps require the main LCD.

### PDA organizer and tools

- [ ] Run `bash scripts/ports/test_teensy41_pda_host.sh`: civil date/leap-year
  coverage, recurrence anchors, recurring task completion, binary CRC validation,
  ICS/vCard round trips and UID merge, dimensional conversions, unique Sudoku,
  Minesweeper first-click/win/loss, app forms/lifecycle and NES direction mapping.
- [ ] Run `python3 scripts/ports/test_teensy41_pda.py --reboot --log /tmp/pda-live.json`
  with idle physical controls, exclusive USB, Ethernet and SD. It backs up and
  restores organizer data, preserves puzzle saves, and uses unique local fixtures.
- [ ] Calendar: month/week/agenda, search, linked contacts, all-day/recurring events,
  birthdays, new/edit/delete/cancel, backup/restore and reopening after reset.
- [ ] Reminder worker: valid RTC/timezone, next-appointment label, catch-up within
  24 hours, reminder while Calendar is closed, short sound, immediate dismissal,
  and no alarm left running after backup restore. Check audio-busy/full-scheduler
  behavior and no automatic power-on wake.
- [ ] Contacts: phone/work/email/address/birthday/category/notes, vCard field limits,
  duplicate UID update and isolation from radio identities. Tasks: priority, due,
  completion, recurrence and original monthly/yearly anchor preservation.
- [ ] Reject malformed/unsupported imports atomically (TZID/UTC, complex recurrence,
  overlong lines/values, capacity overflow); verify interrupted-save backup recovery
  and corrupt-store write blocking. Check full-volume and unavailable-storage errors.
- [ ] Converter: case-sensitive SI prefixes, temperature offsets, explicit US/UK
  volumes and decimal/binary data units. Library: mounted documents, offline manuals,
  filtering, catalog bounds and Reader child-return behavior.
- [ ] Both puzzle games: keyboard and physical on-board/external NES play, cursor
  repeat, saved progress, suspend/resume, dashboard input suppression while owned,
  no held-button leakage and resource conflicts with DMM/Game Boy display ownership.
- [ ] Small 26x16 layout, multiple retained apps and exact heap recovery over cycles.
  Personal backups/exports contain address-book/appointment data; keep test fixtures local.
