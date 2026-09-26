# Teensy 4.1 / SuperKeyboard progress tracker

Last updated: 2026-09-26.

This is the working plan and idea backlog for porting SolarOS from ESP32 to
Teensy 4.1, then bringing up the SuperKeyboard hardware. Keep technical details
and test evidence in the [port notes](teensy41.md).

## Current position

The bring-up baseline is preserved in local commit `b46367f`. The separate
`teensy41_shell` build now runs the real upstream USB shell, app registry and
calculator (interactive text mode and `-e`) with read-only SD access. A reduced
command table and single USB session are implemented; writable storage,
persistent settings and the complete multi-session/service integration remain.
Fitted PSRAM is detected as 8 MiB. Current work is on local `main`.

## Original 13-step plan

Keep these numbers stable when referring to the original plan. The milestones
below group the detailed work; this table records progress against each original
step. Partial implementations and compile checks do not mean full integration.

| Step | Original task | Current status |
| --- | --- | --- |
| 1 | Make an imxrt1062/teensy41 platform target and get the SolarOS core compiling. | Partial: target builds with upstream core lifecycle, queues, parser and expression engine. Full upstream core flavor/services are not yet ported. |
| 2 | Boot FreeRTOS. | Verified on hardware: console task and heartbeat run. |
| 3 | Get USB or Serial1 console output. | Verified over USB. Serial1 is implemented but not hardware-tested. |
| 4 | Get the SolarOS shell prompt. | Verified: upstream `user@teensy41:/` USB shell with reduced command table. Bootstrap retained for recovery; full multi-session integration remains. |
| 5 | Implement SDIO and mount the SD card. | Mount and reads verified, including upstream shell through a read-only libc/directory bridge. Startup failure root cause, hot-removal recovery and writable VFS remain. |
| 6 | Implement the primary display. | Optional RA8875 bring-up compiles. Controller/wiring confirmation, hardware testing and SolarOS terminal/GFX integration remain. |
| 7 | Implement I²C/SPI/UART abstraction. | Initial adapters compile. Hardware tests and upstream service/resource integration remain. |
| 8 | Implement expansion slots. | Initial pin descriptors and exclusive slot claims implemented. Manifest/driver registry integration and hardware tests remain. |
| 9 | Add PSRAM allocation. | Allocation adapter and 4 KiB test implemented. Missing-PSRAM handling and fitted 8 MiB / repeated 4 KiB checks verified; full-capacity testing remains. |
| 10 | Add audio. | Optional SGTL5000/I2S tone bring-up compiles. Wiring/supply checks, hardware tests and audio services remain. |
| 11 | Add secondary display. | Optional ST7735 bring-up compiles. Controller confirmation, hardware tests and second-terminal support remain. |
| 12 | Add USB functionality. | USB CDC console verified. Optional host keyboard support compiles but is untested; other USB roles/features need scope decisions and implementation. |
| 13 | Start enabling higher-level SolarOS applications one at a time. | Full upstream calculator runs in serial text mode and one-shot evaluation through the app registry/lifecycle. Graphics and further applications remain. |

## Next actions

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
- [ ] Bring up the fitted W25Q128JVSIQ flash; no probe or storage test yet.
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
- [ ] Extend the bridge to writable VFS semantics and recovery.
- [ ] Implement persistent configuration storage.
- [x] Integrate the upstream shell and application registry with one USB session.
- [ ] Port full session management and extend supported commands.
- [x] Validate calculator launch/exit, invalid input and memory stability on hardware.
- [x] Flash and verify the separate upstream shell target; retain bootstrap recovery.

First usable shell achieved. Complete when persistent settings, writable storage
and intended session/command coverage also pass their tests.

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
- [ ] Validate the primary display and integrate terminal/GFX rendering.
- [ ] Validate the secondary display and define its terminal behavior.
- [ ] Validate audio output, then input/stream services.
- [ ] Validate USB host wiring and keyboard input; integrate input events.
- [x] Detect fitted 8 MiB PSRAM and pass repeated cache-flushed 4 KiB tests.
- [ ] Test full-capacity PSRAM and define DMA-safe buffer handling where needed.

Done when: confirmed peripherals work through SolarOS APIs, individually and
together. Optional peripheral compilation is not hardware validation.

### 5. Enable useful applications — planned

- [x] Integrate the full calculator application in serial text mode.
- [ ] Enable clock, file viewer/pager, and editor one at a time.
- [ ] Check stack use, allocation failures, and missing-storage behavior per app.
- [ ] Decide which further upstream apps and scripting features to prioritize.
- [ ] Choose Ethernet/Wi-Fi hardware before planning network-dependent apps.

Done when: the selected applications work reliably on the intended hardware.

## Known issues and open decisions

| Item | Status / next step |
| --- | --- |
| SD startup after warm restart | Original failure not reproduced in the first follow-up restart. Added bounded retries and `sdinfo` diagnostics; root cause remains unconfirmed. |
| Missing-card startup delay | Three mount attempts take about 6.2 seconds with the tested firmware/card absent. Console then remains usable. |
| Intermittent USB connection | Removing the USB extension restored enumeration, but two longer tests still lost USB while the board was untouched. Uptime continued across the first reconnection; Linux autosuspend was disabled. A different cable and host port passed 1,000 cycles on 2026-09-26; cause remains unconfirmed. |
| USB console connection timing | Test client needed a one-second settling delay after opening the port. |
| SD hot removal | Recovery after an already successful mount is not implemented. |
| Hardware wiring/population | See unresolved items in the port notes before peripheral bring-up. |
| Network transport | Undecided; not required for the bare-board baseline. |
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
