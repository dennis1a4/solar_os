# Teensy 4.1 / SuperKeyboard progress tracker

**Testing:** [Master outstanding test checklist](teensy41-test-checklist.md).

Last updated: 2026-10-03.

This is the working plan and idea backlog for porting SolarOS from ESP32 to
Teensy 4.1, then bringing up the SuperKeyboard hardware. Keep technical details
and test evidence in the [port notes](teensy41.md).

## Approved workstation sequence — 2026-09-30

Complete and validate one stage before moving to the next:

1. Retained application sessions, suspend/resume, `fg` and `close` — implemented; automated checks pass;
   user confirmation passed.
2. Background jobs and scheduling (`jobs`, `job`, `schedule`) — implemented; host/device acceptance passes. See [jobs notes](teensy41-jobs.md).
   Stage 2b: detachable MicroPython worker (`Ctrl+Z`/`bg`/`fg`), retained input/output,
   safe stop and `tail` — installed; final-image workflow, edge, network and graphics acceptance passes.
3. Network diagnostics and clock synchronization (`ntp`, `ping`, `netscan`) — implemented, installed and host/device acceptance passed; see [diagnostics notes](teensy41-network-diagnostics.md).
4. Hardware resource management and terminal access (GPIO/buses, `io`,
   `expansion`, `com`, with pin ownership) — installed; host/device acceptance and physical UART8 loopback pass. See [hardware notes](teensy41-hardware-resources.md).
5. PSRAM-backed temporary storage (`ramfs`) — installed; host/device checks pass. See [RAMFS notes](teensy41-ramfs.md).

The fifth stage is item 7 from the command-gap review. Monitoring and transfer
features remain backlog; they do not precede RAMFS in this approved sequence.

## Current position

**2026-10-03 Serial terminal/logger:** installed shared native COM capture with
background raw/timestamped logging, baud/framing configuration, CR/LF/CRLF,
text/hex display and optional XON/XOFF. Each recording uses 64 KiB PSRAM and each
terminal 4 KiB; control tasks have fixed internal stacks. Host and device
acceptance pass; physical RX/framing/flow and sustained load remain pending.
RTS/CTS needs pin integration and hardware validation. See the hardware notes.

**2026-10-03 Python foundation:** resource-managed machine GPIO/bus/ADC/PWM
subset and os/time helpers are implemented with an installed offline library
bundle and pinned full upstream source archive on SD. Physical peripheral tests
remain pending. Future work: MIDI/audio/USB-PD/CAN Python bindings, broader
standard-module compatibility and additional validated packages. See handoff.


**2026-10-03 upstream 4.15.18:** memory improvements, interactive `ltop`,
WebRadio, SSH key commands, MIDI recording/playback and STUSB4500 software are
integrated. Existing bus tools are retained. Physical audio/MIDI/PD verification
is pending; see the [handoff](teensy41-handoff.md) and master checklist.
MIDI viewer/editor and standard MIDI file import/export remain future work.

**2026-10-01 revised PCB pinout:** recorded separately from the unchanged bench
profile. RGB0/AmpEn1/DMM40 are the new PCB targets. Flash-drive `Pins_v3.ods`
is verified; header 2 is display-only, pin 9 is backlight PWM, and pin 33's extra
Motor mark is stale. Resolve display-specific wiring before a PCB profile.
See [pinout and firmware implications](superkeyboard-pcb-pinout.md).

**2026-10-01 Python highlighting:** shared lexer gains built-ins, definitions
and constants, with TUI colors and incremental editor caches. Host/device
acceptance passes; see [syntax notes](teensy41-syntax.md).

**2026-10-01 RAMFS:** original item 7 is installed. The approved workstation
sequence is complete within documented limits; choose the next feature from
the command audit/backlog.

**2026-09-30 hardware resources:** stage 4 is complete within the fixed-routing
scope documented above. RAMFS followed on 2026-10-01.

**2026-09-30 shared completion:** cursor-aware Tab completion and bounded
on-demand filesystem providers are implemented; reusable C/TUI and raw-field
APIs are available. See [completion notes](teensy41-completion.md). This user-requested
addition does not replace stage 4 in the approved sequence.


**2026-09-29 keyboard repeat:** implemented and flashed on AmpEn40 legacy wiring;
400 ms delay / 33 ms interval. Host sanitizer and child lifecycle tests pass.
User confirmed basic physical repeat/release; reconnect and broader app checks
remain pending. See [keyboard notes](teensy41-keyboard.md).

**2026-09-29 workstation shell:** embedded Help/Man, Watch, task/system/storage
diagnostics, Date/Time, archives and Curl added and tested on legacy wiring.
Retained app sessions and fg/close were added on 2026-09-30; see
[session notes](teensy41-sessions.md). See the [complete command audit](teensy41-workstation.md). Current state
is the stage 4 legacy image; query network status after its reboot.

Current firmware, validation and recovery instructions are in the
[handoff](teensy41-handoff.md); older pause/unplug entries are historical.

**Telnet installed:** password-protected incoming Ethernet shell alongside USB
and LCD, one remote client. Host/device tests pass, including disconnect cleanup,
network recovery, immediate restart and stable warm idle memory over ten cycles.
The user also confirmed their own Telnet connection worked.
`teensy41_telnet_legacy` preserves AmpEn40/Serial1 and
disables physical scope ADC until rewiring; both scope/pdpower demos are installed.
See [Telnet notes](teensy41-telnetd.md).

**Scope candidate:** graphical `scope` adds single-channel timer/DMA snapshots
and demo mode. Positive 5/50 V manual scaling, triggering, run/hold and basic
measurements. Pin 40 ADC / pin 0 AmpEn; Serial1 disabled in display build.
Host tests pass; demo installed in legacy image. Rewiring, normal display-image
flashing and ADC hardware validation pending. See
[scope notes](teensy41-scope.md).

**USB-PD power candidate:** native `pdpower --demo` text app and negotiation model
implemented with simulated success/fault scenarios. Host tests pass; installed
with Telnet (device interaction checks pending).
STUSB4500 I2C backend and board limits pending; motor controller deferred. See
[power notes](teensy41-power.md).

**Clock/timezone installed:** shared Clock app ported with time, stopwatch and
transient countdown modes. Saved timezone support includes fixed UTC-5 Manitoba.
Host and remote device tests pass, including reboot persistence and repeated
launch memory recovery. Flashed; visual/audio checks pending (audio shield missing).

**Keyboard testing:** Earlier reconnect checks passed. Repeat is now implemented
and basic hold/release is user-confirmed. Held-key removal on the new image,
multiple simultaneous holds and wider app checks remain in KEY-2 through KEY-5.

**2026-09-28, OBD demo:** Native `obd --demo` and shared bounded CAN/ISO-TP/OBD
foundation implemented and host-tested. Installed with the clock image; real
CAN hardware remains untested.
MCP2515 hardware backend and separate canmon app remain next steps. See
[OBD notes](teensy41-obd.md).

**2026-09-28, SD recovery:** implemented, host-tested and installed. Boot mount
passes; physical hot-removal validation remains pending.
Guided SD/USB/keyboard reconnect automation is ready for a local operator.
Physical validation remains pending; see [SD recovery](teensy41-sd-recovery.md).

**2026-09-28, MQTT Explorer:** Native `mqttx` adds topic tree/latest values,
message history, payload inspection, pause/filtering and SD logs. Isolated
hardware capture/reconnect/keepalive and memory checks pass. Authenticated live
capture and user LCD/navigation checks also passed; see the
[MQTT notes](teensy41-mqtt-explorer.md). Initial transport is MQTT 3.1.1 TCP.

**2026-09-28:** Python graphics and View passed host/device checks and user
visual confirmation. Invaders physical movement/fire controls also confirmed.
Small redraws take 33–36 ms; full-screen transfers remain about 1.74 seconds.

The current `teensy41_display` profile runs independent LCD/USB-host-keyboard
and USB serial shells, extending the shared Files/SSH/network/storage/apps
profile and terminal synth. Temporary Adafruit RA8875 wiring and the tested
Microsoft keyboard are documented in the [Teensy README](README.md).
Physical typing and Files navigation work. Dual-session isolation, cancellation,
USB reconnect and app cleanup passed. The audio shield was removed for access
to the USB host cable, so combined audio/display hardware validation remains.

Writable SD/QSPI flash, persistent identity/USB geometry/startup selection,
MicroPython, Ethernet, SSH client, calculator, editor, Files, less, Notes and
Sheet are integrated. The display uses the shared TUI apps through an ASCII/ANSI
terminal. Shared pixel graphics, View and Python `solaros.gfx` are now integrated;
full upstream session/service coverage remains. See [graphics notes](teensy41-graphics.md).
Fitted PSRAM is 8 MiB. Compatibility remains the priority: keep application logic
shared and put board changes beneath OS services. Current work is on branch `teensy41`.

## Original 13-step plan

Keep these numbers stable when referring to the original plan. The milestones
below group the detailed work; this table records progress against each original
step. Partial implementations and compile checks do not mean full integration.

| Step | Original task | Current status |
| --- | --- | --- |
| 1 | Make an imxrt1062/teensy41 platform target and get the SolarOS core compiling. | Partial: target builds with upstream core lifecycle, queues, parser and expression engine. Full upstream core flavor/services are not yet ported. |
| 2 | Boot FreeRTOS. | Verified on hardware: console task and heartbeat run. |
| 3 | Get USB or Serial1 console output. | Verified over USB. Serial1 is implemented but not hardware-tested. |
| 4 | Get the SolarOS shell prompt. | Verified: independent LCD and USB upstream shells with a reduced command table. Bootstrap retained for recovery; full upstream session coverage remains. |
| 5 | Implement SDIO and mount the SD card. | Mount and reads verified, including writable shell/SD file operations, editor saves and Python files. Startup failure root cause, physical validation of the new hot-removal recovery candidate and full VFS semantics remain. |
| 6 | Implement the primary display. | Adafruit RA8875 800x480 text terminal and shared TUI apps verified on temporary wiring. Shared pixel graphics/View/Python are integrated; final PCB wiring remains. |
| 7 | Implement I²C/SPI/UART abstraction. | Fixed-bus commands, shared resource claims and COM UART adapter installed; physical UART8 loopback passes. External I²C/SPI devices and broader bus service APIs remain. |
| 8 | Implement expansion slots. | Fixed descriptors and protected SPI CS leases installed, with independent UART leases. Manifest/driver registry and external peripheral tests remain. |
| 9 | Add PSRAM allocation. | Allocation adapter and 4 KiB test implemented. Missing-PSRAM handling and fitted 8 MiB / repeated 4 KiB checks verified; full-capacity testing remains. |
| 10 | Add audio. | Rev D shield headphone output and aplay MP3/WAV verified in teensy41_audio. Mic WAV capture verified with speaker tone; 60 Hz hum remains. Custom-board supply/wiring and full audio services remain. |
| 11 | Add secondary display. | Optional ST7735 bring-up compiles. Controller confirmation, hardware tests and second-terminal support remain. |
| 12 | Add USB functionality. | Independent USB CDC console, Microsoft USB host keyboard and 16 GB FAT32 USB mass storage on a powered hub verified. `/usb` mounting, file operations and safe eject/remount pass; other USB roles remain. |
| 13 | Start enabling higher-level SolarOS applications one at a time. | Calculator, upstream editor and a Teensy MicroPython adapter run through the registry/lifecycle. View, Invaders and Python graphics are integrated. Hardware Python bindings and further applications remain. |

## Next actions

- [x] Add workstation commands and keyboard repeat; basic repeat/release confirmed.
- [ ] Complete remaining physical keyboard checks in the master checklist.
- [x] Implement bounded retained app sessions/fg/close and owner-console requests.
- [ ] Make cold DF scans cooperative.

The earlier bring-up follow-ups below remain open where unchecked.

- [x] Test a cold power cycle with the SD card inserted (baseline passed).
- [x] Repeat the cold power cycle with the new retry/diagnostic firmware
  (first-attempt mount in 390 ms after removing the USB extension).
- [ ] Investigate why automatic SD mounting failed after one reflash, while
  a subsequent `mount` command succeeded.
- [x] Test boot without an SD card; console/calculator remain usable.
- [x] Test mounting a card inserted after boot (first attempt, 390 ms).
- [x] Complete the 1,000-cycle console/calculator/SD-read test (2026-09-26).
- [ ] Run a longer USB/SD soak; the passing 1,000-cycle run lasted 154 seconds.
- [x] Repeat the unchanged firmware/SD workload with another USB data cable
  and port: 1,000 cycles passed. Both changed together; the earlier disconnect
  cause is still unconfirmed. Try another computer if failures recur.
- [ ] Test PSRAM beyond the repeated 4 KiB allocation check.
- [x] Detect the fitted 16 MiB Winbond QSPI flash, initialize only verified-blank media, and test LittleFS storage.
- [x] Preserve the tested bring-up baseline in version control (`b46367f`).

## Milestones

### 1. Establish a reliable bare-board baseline — in progress

- [x] Add a separate Teensy 4.1 PlatformIO target and FreeRTOS runtime.
- [x] Compile the SolarOS core subset and pass host regression tests.
- [x] Flash the board and verify the USB bootstrap console.
- [x] Verify heartbeat progress, queue operation, and positive stack headroom.
- [x] Exercise calculator, parsing, editing, and input-error handling.
- [x] Mount SD, list files, read an existing text file, and handle missing paths.
- [x] Handle missing PSRAM without falling back for external-required memory.
- [x] Fix heap accounting and pass 200 calculations with stable reported heap.
- [x] Complete cold-start, missing-card, and 1,000-cycle stability checks above.
- [x] Make hardware smoke tests repeatable without depending on personal SD files
  (`scripts/ports/test_teensy41_serial.py`; see the adjacent README).

Done when: boot and console are repeatable, SD failure/recovery behavior is
understood, and a tested baseline is saved with reproduction instructions.

### 2. Integrate the real SolarOS shell and storage — in progress

- [x] Add a core shell profile, headless I/O, and one USB session adapter.
- [x] Bridge read-only SD files/directories to upstream shell filesystem calls.
- [x] Add writable SD streams/descriptors and test file operations, editor saves and Python I/O.
- [x] Add SD/flash routing beneath common file APIs, including cross-volume file copy/move.
- [x] Add USB mass-storage `/usb` mounting, root/Files enumeration, file operations and safe eject/remount (16 GB FAT32, powered hub).
- [ ] Finish physical USB hot-removal validation, SD hot-removal recovery and remaining VFS semantics.
- [x] Implement persistent configuration storage for identity, USB geometry and startup selection.
- [ ] Extend persistent preferences as further time/network/display services are ported.
- [x] Integrate the upstream shell and application registry with one USB session.
- [ ] Port full session management and extend supported commands.
- [x] Validate calculator launch/exit, invalid input and memory stability on hardware.
- [x] Flash and verify the separate upstream shell target; retain bootstrap recovery.

First usable shell, writable storage and initial persistent preferences achieved.
Full intended session/command coverage and storage recovery still remain.

### 3. Integrate buses and expansion — planned

- [x] Implement initial bus adapters, pin descriptors, and slot claims.
- [ ] Connect bus adapters and slot ownership to the SolarOS resource model.
- [ ] Test I²C, SPI, and UART using known devices or loopback fixtures.
- [ ] Integrate expansion manifests and driver registration.
- [ ] Validate shared-bus locking and conflicting resource requests.

Done when: a known expansion device works through SolarOS services and resource
conflicts are handled predictably. External hardware is required.

### 4. Bring up SuperKeyboard peripherals — awaiting hardware checks

- [ ] Resolve the wiring and component questions listed in the port notes,
  especially SGTL5000 supply voltage, display controllers, and GPIO9's role.
- [x] Validate the Adafruit primary display and integrate terminal/TUI rendering.
- [x] Integrate shared pixel graphics, View and Python graphics on the temporary RA8875 wiring.
- [ ] Validate final PCB display wiring.
- [ ] Validate the secondary display and define its terminal behavior.
- [x] Validate Rev D shield output and upstream aplay MP3/WAV playback.
- [x] Verify Rev D microphone WAV recording, cancellation and speaker-tone capture.
- [ ] Investigate 60 Hz microphone hum and verify clean recording quality.
- [ ] Validate custom-board audio wiring and full stream services.
- [x] Validate USB host input with the Microsoft keyboard and local terminal.
- [ ] Investigate the home-built keyboard and broader HID compatibility.
- [x] Detect fitted 8 MiB PSRAM and pass repeated cache-flushed 4 KiB tests.
- [ ] Test full-capacity PSRAM and define DMA-safe buffer handling where needed.

Done when: confirmed peripherals work through SolarOS APIs, individually and
together. Optional peripheral compilation is not hardware validation.

### 5. Enable useful applications — in progress

- [x] Integrate the full calculator application in serial text mode.
- [x] Enable and hardware-test the upstream editor, including protected replacement saves.
- [x] Add MicroPython REPL, SD scripts/imports, file I/O and Ctrl-C cancellation.
- [x] Enable and hardware-test the less text pager, including search and Files return.
- [x] Enable and hardware-test Notes checklists and Sheet CSV viewing/formulas.
- [x] Enable clock (time, stopwatch, transient countdown and saved timezone); visual/audio validation pending.
- [ ] Add selected Teensy hardware bindings to Python after peripheral integration.
- [ ] Check stack use, allocation failures, and missing-storage behavior per app.
- [x] Prioritize on-device editing and MicroPython scripting.
- [ ] Choose further apps and Python modules as needed.
- [x] Choose native Teensy 4.1 Ethernet with the PJRC Ethernet kit.
- [x] Verify Ethernet link, DHCP, DNS, TCP, software restart and short audio/shell coexistence.
- [x] Test Python blocked receive during physical cable removal and HTTP recovery after reconnection.
- [ ] Run long Ethernet DHCP/traffic soak.
- [x] Add and hardware-test MicroPython IPv4 TCP sockets, DNS, HTTP-to-SD and cleanup.
- [x] Connect Ethernet to the shared network registry and managed TCP/UDP socket service.
- [x] Reuse the existing `solaros.net` Python binding implementation and verify its API on hardware.
- [ ] Port direct BSD/lwIP socket consumers, TLS/WebSockets and further network services.
- [ ] Enable additional upstream apps through shared services, minimizing app-specific changes.

Done when: the selected applications work reliably on the intended hardware.

## Known issues and open decisions

| Item | Status / next step |
| --- | --- |
| SD startup after warm restart | Original failure not reproduced in the first follow-up restart. Added bounded retries and `sdinfo` diagnostics; root cause remains unconfirmed. |
| Missing-card startup delay | Three mount attempts take about 6.2 seconds with the tested firmware/card absent. Console then remains usable. |
| Intermittent USB connection | Removing the USB extension restored enumeration, but two longer tests still lost USB while the board was untouched. Uptime continued across the first reconnection; Linux autosuspend was disabled. A different cable and host port passed 1,000 cycles on 2026-09-26; cause remains unconfirmed. |
| USB console connection timing | Test client needed a one-second settling delay after opening the port. |
| SD hot removal | Recovery is implemented and host-tested; physical removal/reinsertion validation remains pending. |
| Hardware wiring/population | See unresolved items in the port notes before peripheral bring-up. |
| Network transport | Native Ethernet with PJRC kit selected; teensy41_network link/DHCP/DNS/TCP and short audio/shell checks passed. Shared interface registry, managed TCP/UDP services and solaros.net now work; direct POSIX/ESP networking and broader service coverage remain; SSH and Curl TLS adapters are integrated. |
| MicroPython scope | 512 KiB PSRAM heap, basic REPL and SD scripts/imports/files; IPv4 TCP socket module plus shared solaros.net TCP/UDP APIs in the network profile; no CircuitPython or hardware modules yet. |
| Full upstream feature scope | Select incrementally after shell/storage integration. |

## Future ideas inbox

Add rough ideas here without committing them to the implementation plan. Move
an idea into a milestone when its priority and hardware needs are clear.

| Idea | Why it would be useful | Hardware/dependencies | Priority / decision |
| --- | --- | --- | --- |
| _Add ideas here_ | | | |

## Progress log

- **2026-09-20:** Initial port compiled; host tests and optional peripheral
  compile checks passed. No hardware tests at that point.
- **2026-09-24:** First bare-board flash and USB/SD/calculator tests passed.
  Corrected heap accounting and reflashed; 200-calculation smoke test passed.
  Recorded intermittent startup SD mount failure for follow-up.
- **2026-09-24, follow-up:** Baseline cold boot with card, warm restart, and
  cold boot without card passed. Added three-attempt mount limit and `sdinfo`;
  flashed and verified missing-card failure/console operation. Added a reusable
  read-only serial test script. Insertion after boot and manual mounting passed;
  final cold start passed after removing the USB extension. Two longer stability
  runs were interrupted by USB disconnects; a different cable is pending.
- **2026-09-24, stopping point:** User has no other data-capable micro-USB cable
  available today and plans to bring one tomorrow. Hardware stability testing
  is waiting for that comparison; do not mark the longer runs as passed.

- **2026-09-26:** Different cable and host port passed the unchanged 1,000-cycle
  calculator/SD-read workload in 154 seconds. Fitted PSRAM detected as 8 MiB;
  101 cache-flushed 4 KiB checks passed alongside 100 further SD/calculator
  cycles, with stable reported free memory. Added `--psram` to the test client.
  User reports fitted W25Q128JVSIQ flash; flash testing and broader RAM coverage
  remain pending. No firmware upload or commit performed.

- **2026-09-26, upstream shell:** Preserved recovery baseline in `b46367f`.
  Built and flashed `teensy41_shell`: shared shell, app registry, full text
  calculator, one USB session and read-only SD libc bridge. Interactive tests
  and 1,000 calculator/read cycles passed in 103.774 seconds with
  stable internal/external free memory. Recovery build and host regressions
  passed. Persistent settings, writable storage and full sessions remain next.

## Keeping this useful

Check items off only when their stated result has been verified. Add dated
evidence to the port notes, update the next actions when priorities change,
and keep uncommitted ideas in the inbox. Record compile-only results separately
from on-board tests.

### Writable SD and on-device scripting — 2026-09-26

- Added writable SD bridge and `mkdir`, `cp`, `mv`, `rm`; copy/move refuse an
  existing destination. Enabled the upstream editor with PSRAM buffering,
  temporary-file saves, dirty-exit confirmation and serial TUI support.
- Integrated the bundled MicroPython engine with a synchronous Teensy runtime,
  512 KiB PSRAM heap, basic REPL, SD source files/imports, file streams,
  exceptions/GC, and Ctrl-C interruption. GPIO/display/network Python APIs
  remain outside this first integration.
- Hardware suite passed editor create/save/replace/reopen, file operations,
  interpreter imports and file modes, 300 KB allocation/GC, infinite-loop
  interruption, error/exhaustion recovery, and 50 editor/Python lifecycle
  cycles with unchanged reported free memory. See port notes for logs and
  additional persistence/regression results.
- Added manual serial geometry via `setterm size COLS ROWS`; terminal-size
  negotiation and physical keyboard/display operation remain future work.
- Saved files and Python execution survived a software reboot. Final 100-cycle
  shell/calculator/read regression and recovery build passed. Cold power-cycle
  testing of this writable profile remains separate from the earlier baseline.

### Rev D audio shield / aplay — software prepared, wiring pending

- User identified a PJRC Rev D shield, currently disconnected, and is wiring
  it to the Teensy. No audio firmware upload or audible test has happened yet.
- Added a separate `teensy41_audio` profile with SGTL5000 headphone output,
  one-second tone/status commands, buffered stereo I²S, and the real `aplay`
  application using the bundled MP3 decoder and sample-rate converter.
- Software build and host decoder/transport tests are the current scope;
  hardware detection, tone, SD MP3/WAV playback, cancellation, stack/memory
  stability and listening confirmation must pass before marking audio done.
- Next: confirm wiring and listening output, close the serial monitor, upload
  the audio profile, and run the generated-tone hardware suite.

### Rev D audio output / aplay verified — 2026-09-26

- Shield connected and detected; user heard clear, quiet tones through a
  battery-only portable speaker on the headphone jack.
- Flashed `teensy41_audio`; stereo MP3, resampled mono MP3 and WAV passed with
  zero initial underruns. Cancellation/error recovery and 20 additional
  playback cycles passed with stable memory and positive stack headroom.
- The board is running the audio profile. The standard shell and its saved
  recovery image remain available. Recording, line-out, full audio services
  and custom SuperKeyboard codec hardware remain follow-up work.

### Ethernet connected to shared OS services — 2026-09-26

- Built the actual upstream interface registry and managed socket session service
  into `teensy41_network`. Ethernet publishes link/address/DNS readiness as `eth0`;
  shared `network interfaces` and `network routes` expose the selected path.
- Removed the socket service's Wi-Fi-only readiness dependency. Added a transport
  boundary below its existing ownership, quotas, timeout and cleanup logic.
- Extracted the existing Python network handlers into a shared include used by
  both runtimes. The Teensy runtime registers the same `solaros.net` API;
  unsupported WebSockets, ping and router operations explicitly report errors.
- Hardware tests passed TCP, UDP (including empty/fragmented/truncated datagrams),
  timeouts, Ctrl-C, stale handles, quotas, repeated interpreter cleanup and restart.
  Existing standard Python TCP/HTTP-to-SD behavior also passed.
- This is source/API compatibility for the enabled services. Native apps still
  compile into firmware; arbitrary ESP binaries, direct ESP/lwIP/POSIX networking,
  TLS, persistent route settings and other SolarOS Python namespaces are not enabled.

- Final network and shell-only builds passed. MP3/WAV and three repeat audio
  cycles also passed with Ethernet active. Saved the tested image under
  `../solar_os-baselines/2026-09-26-network-services/`; see the port notes for
  exact build sizes, SHA-256 and test logs.

### Fitted QSPI flash / shared storage — 2026-09-26

- The driver detects 16 MiB of Winbond flash (reported label `W25Q128JV*M (DTR)`).
  A read-only full-chip scan found it erased. Explicit blank-only initialization
  created LittleFS; startup never automatically formats after a failed mount.
- `teensy41_network` routes existing file APIs to `/sd` or `/flash` while keeping
  legacy SD paths under `/`. Editor, Python and copy/move commands use the common
  storage adapter. Cross-volume file moves copy/close before deleting the source;
  raw POSIX rename and cross-volume directory moves remain unsupported.
- Hardware verified binary copies in both directions, cross-volume file moves,
  append/update/truncate/exclusive-open modes, descriptor exhaustion protection,
  editor save/replacement, Python execution and cleanup. Files survived a firmware
  update and software reboot; no initialization was needed afterward.
- Full-capacity write/erase endurance, cold power-cycle/power-loss tests and
  missing-SD operation of this new profile remain separate follow-up checks.
- Combined Ethernet/flash testing exposed a BusFault during a large Python
  PSRAM-buffer read. Added internal-RAM staging around QSPI transfers; the rerun
  passed 20 large read/write/hash cycles and MP3/WAV playback from flash.
  The same HTTP example runs from either volume and saves verified downloads.
  Increased the flash profile's console stack and native-call reserve beneath
  Python's recursion limit for nested Python/network/LittleFS operations.
- Final recursion/file stress retained 1,969 stack words; reboot persistence and
  shared TCP/UDP regression passed. Tested firmware and logs are saved in
  `../solar_os-baselines/2026-09-26-flash-storage/`.

- Root listing corrected: `/` shows mounts only; SD files are listed under `/sd`.
  Legacy SD file paths still resolve. Hardware listing/read regression passed.

### SSH client — 2026-09-26

- `teensy41_ssh` builds the shared SSH app/session/crypto services over the existing
  Ethernet transport. Added foreground worker/tick support and shared TRNG access.
- Hardware password login, terminal I/O, host-key mismatch rejection and
  cancellation passed. Fixed nonblocking cleanup leaks; repeated sessions recover
  memory. Public-key auth and broader server interoperability remain untested.
- Files is now integrated using the existing storage/TUI services; see below.

### Files and combined-app compatibility — 2026-09-27

- Enabled the shared Files app and ZIP service in `teensy41_files`.
- Added mount enumeration/virtual-root semantics and bounded child-app return
  in the USB runtime. App frames live in PSRAM and retain arguments and TUI state.
- Hardware passed SD/flash copy and file move in both directions, recursive copy,
  mkdir/delete, validated ZIP creation, copy cancellation/partial cleanup, failed
  Python child return, repeated editor return and stable memory/handle cleanup.
- Corrected the MPU heap boundary, reserved more internal RAM by placing shell/UI
  code in flash, enabled full integer formatting, and mapped worker priorities
  below USB/Ethernet so long file jobs remain cancellable.
- Final combined SSH/network/storage/audio regressions passed. Saved the tested
  firmware, logs and source snapshot in `../solar_os-baselines/2026-09-27-files/`;
  working-tree changes remain uncommitted. Cross-volume directory moves and full
  multi-session support remain outside this integration.

### Persistent settings and useful text apps — 2026-09-27

- Added `teensy41_apps`, extending the tested Files image with flash-backed
  identity, USB terminal geometry and startup-source preferences. Reboot
  persistence and once-per-boot startup execution passed on hardware.
- Integrated unchanged upstream less, Notes and Sheet using existing services.
  Fixed shared text-widget CR handling. Both storage volumes, search, Notes
  persistence, CSV formulas, Files return and 20 lifecycle cycles passed.
- Settings fault/corruption tests and child lifecycle sanitizers passed, as did
  manual generation, widget and core regressions. Combined SSH, Files, TCP/UDP,
  flash/Python/editor and MP3/WAV regressions passed on the final image.
- Saved `../solar_os-baselines/2026-09-27-apps/`; preferences restored and temporary
  startup fixture removed. No commit or push. Physical keyboard/display, clock,
  broader settings, SD hot-removal and full sessions remain future work.

### Synth and independent LCD/USB terminals — 2026-09-27

- Shared eight-voice synth engine runs through the SGTL5000 output worker with
  terminal controls. Host sanitizers, device waveforms/load, twenty restarts,
  USB disconnect and MP3/WAV regression passed on the synth firmware.
- Added configurable RA8875 wiring, Adafruit display preset, buffered ANSI
  terminal, HID parsers and keyboard navigation/function keys.
- Combined display profile runs two real shell sessions with per-session app
  frames, shared app/resource ownership, and owner-specific cancellation.
- Dual-session and Files regressions passed. User confirmed local typing and
  Files navigation with Microsoft keyboard 045e:0750. Home-built keyboard remains
  unverified; audio shield was removed for USB host access.
- See [display notes](teensy41-display.md) and [synth notes](teensy41-synth.md).
  Touch is not a priority; pixel graphics and broader services remain future work.
