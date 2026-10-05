# Teensy / SuperKeyboard handover

Updated 2026-10-05. Branch: `teensy41` (upstream integration retained as `teensy41-upstream-4.15.18`); GitHub: `dennis1a4/solar_os`.
This is the current state. Older snapshots are in the
[handover history](teensy41-handoff-history.md).

## Live audio acceptance and WebRadio recovery — 2026-10-05

User reconnected SGTL5000, microphone and a nearby speaker, with USB host/drive/
keyboard disconnected. Codec became ready after reboot. The following actual
hardware tests passed on legacy bench wiring:

- Five one-second WAV playback/recording cycles: zero idle rings, exact heap
  recovery (373,180 internal / 8,123,868 PSRAM bytes).
- Generated stereo MP3, mono 48 kHz MP3 and mono 22.05 kHz WAV playback;
  resampling, cancellation, errors and 20 repeats: zero playback underruns.
- Synth waveforms, pulse/hold, eight voices with filter/second oscillator,
  cancellation and 20 restarts: zero reported underruns/missed deadlines/errors.
- Four-second microphone capture into RAMFS, cancellation/finalized WAV header
  and overwrite protection. Acoustic tone detection FAILED: dominant 60 Hz hum,
  only 1.83 dB increase in 430–450 Hz energy, no clipping. User was away and
  requested deferring the audible speaker check; do not claim acoustic quality.

Storage reliability findings (same 32 KiB capture queue):

| Destination | Requested | Wall time | Result |
| --- | ---: | ---: | --- |
| SD | 4 s | 4.19 s | Complete; no overruns |
| SD | 10 s | 1.26 s | Failed at 0.58 s of samples; 44 overruns |
| SD | 30 s | 21.12 s | Failed at 20.45 s of samples; 43 overruns |
| Flash | 4 s | 6.62 s | Full WAV; no queue overruns, excess elapsed time |
| Flash | 10 s | 15.54 s | Full WAV; no queue overruns, excess elapsed time |
| Flash | 30 s | 46.79 s | Full WAV; no queue overruns, excess elapsed time |

These are NOT passes for continuous recording. Flash elapsed time includes
open/header/final sync, so it does not locate the delay precisely. The driver
also masks interrupts during page programs; zero queue overruns cannot prove
no samples were lost before queue insertion. SD needs a larger, independently
drained staging queue or another verified latency fix. Flash needs interrupt/
write-latency investigation. USB recording remains untested with host unplugged.

Added `audio monitor NEWFILE.wav`: four seconds of microphone capture without
stopping existing playback, for use on a second console and preferably RAMFS.
Capture busy/retained-recorder protection, buffer preservation and manuals pass
host tests. Installed monitor image SHA256:
`cc447ce64c508bb3d5f0968aaed3e9285dd9c62614e83ddae9078239d8da16f7`.

Actual WebRadio decoding from a controlled local HTTP MP3 stream then triggered
`STACK OVERFLOW: webradio_`, halting the board before its monitor capture. The
Teensy worker now requests 32 KiB PSRAM instead of 20 KiB and logs its minimum
stack headroom; ESP configuration is unchanged. Corrected candidate builds:
RAM1 431,104, RAM2 187,256, flash 1,472,616 bytes; HEX SHA256:
`5ff456c046b218317097f87765dcd75791b9f259804ac690630e1889b1afb092`.
**Candidate upload is waiting for the physical Program button: USB soft reboot
failed on the halted board. Stack correction and WebRadio acoustic/counter tests
are NOT device-validated yet.** Upload log `/tmp/teensy-radio-stack-upload.log`.

Resume: press Program, wait for upload completion, repeat controlled HTTP radio
and HTTPS station tests, inspect headroom/underruns and monitor WAV, then synth
USB-disconnect cleanup. Defer physical speaker/mic quality checks until user is
present. Remove only retained failed fixture `/sd/_solaros_mic_f588b304d9.wav`;
other successful SD/flash fixtures were removed, RAMFS disappears on reboot.

Evidence: `/tmp/teensy-audio-rings-live.json`, `/tmp/teensy-audio-play-live.json`,
`/tmp/teensy-synth-live.json`, `/tmp/teensy-mic-ram-live.json` and `.wav`,
`/tmp/teensy-audio-storage-live.json`, `/tmp/teensy-radio-local-live.json`.
Reusable procedures are in scripts/ports/README.md. No saved stations changed.

## On-demand audio rings — 2026-10-05

Implemented and installed on unchanged `teensy41_telnet_legacy` bench wiring.
The 16 KiB playback and 32 KiB microphone rings allocate from internal memory
on start and release after stop/error cleanup. AudioStream interrupt exclusion
protects pointer publication/detachment; allocator operations run outside it.
DMA/library buffers remain unchanged. Standalone tones and clock alarms do not
allocate rings. `audio status` reports each ring's allocated bytes.

Diagnostic commands cannot reset a buffer owned by an active/retained audio app
on USB, LCD or Telnet. Microphone diagnostics block competing audio app starts
while yielding to other consoles. Actual-code host tests cover these ownership
rules and 100 buffer lifecycle cycles with allocator/configuration failure,
cancellation, timeouts, overflow, sample checks and injected interrupts at
handoff. Sanitizers, existing audio-file tests and 15 manual tests pass.

Final device test passes missing-codec error paths, five playback/recording
attempts, zero idle ring bytes and exact memory recovery; its unique SD fixtures
were removed. Codec still reports missing: live capture/playback, synth, analog
sound and loaded coexistence are not claimed. See the master test checklist.

Final free internal heap: **373,180 / 421,528 bytes** (about 364.4 KiB free),
including OCRAM 336,888 / 336,888 and DTCM 36,292 / 84,640. This is 48 KiB more
idle heap than the prior memory revision; PSRAM stays 8,123,868 / 8,388,608.
Build RAM1 431,104 bytes; RAM2 187,256 bytes; program flash 1,471,928 bytes.
HEX SHA256: `010e0f9dd73e61148462de83bbc652941b14e43a8274d92a58f9655101bbfc1c`.
Logs: `/tmp/teensy-audio-rings-device.json`, `/tmp/teensy-audio-rings-build.log`,
`/tmp/teensy-audio-rings-upload.log`. Details: [audio lifecycle](teensy41-synth.md).

Recording duration and buffer capacities are unchanged: WAV streams to storage
with the existing one-hour limit. A future larger PSRAM queue would absorb
longer storage stalls, not automatically extend that duration limit.

## More available internal RAM — 2026-10-05

Installed on the unchanged `teensy41_telnet_legacy` bench wiring. Telnet's
40 KiB stack, the foreground worker's maximum 28 KiB stack, and Python's
40 KiB background stack now allocate when needed and free after their tasks
finish and suspend. Another task deletes/reaps them; a running stack is never
freed. Telnet client `exit` retains the listener; `telnetd stop` releases its
stack after cleanup. `telnetd status` shows stack bytes and pending cleanup.

The allocator now exposes the previously unused linker-defined OCRAM tail,
excluding static/DMA buffers and unwind tables. Internal-preferred allocations
use OCRAM first, with guarded DTCM fallback. Critical allocations prefer DTCM.
Ordinary libc malloc remains DTCM-only; this is not automatic swapping.
`mem` reports combined internal free memory plus separate DTCM/OCRAM figures.
Generic OCRAM allocations are not DMA-coherent buffers. Existing PSRAM policy
and reserve are unchanged; these stacks remain in internal RAM.

Measured cold idle free internal memory increased from 36,324 to 324,028 bytes
(about 35.5 to 316.4 KiB). This combines 108 KiB of removed static stacks and
previously unused OCRAM. PSRAM remains 8,123,868 bytes free. RAM1 is 430,080 bytes,
RAM2 236,408 bytes (110,592 fewer static bytes); program flash is 1,470,144 bytes.
Installed HEX SHA256:
`a3cc816e6b40fcdb65906d06415499d7180fb15cf9bdce95c78db7736c5e0cb9`.

Host allocator and task-lifetime tests cover allocation/task-creation failure,
alignment, repeated cycles, safe deletion, exact recovery and DMA refusal.
Device Telnet regression covers authentication, reconnects, self-stop, stopping
remote Python and exact OCRAM recovery. Python process regression covers
suspend/resume, cross-console use, repeated cancellation and memory recovery.
Graceful shutdown regression also passes, including blocked shutdown, Python
finally handlers, refusal timeout and script/serial cleanup with memory recovery.
Logs: `/tmp/teensy-lazy-telnet.json`, `/tmp/teensy-lazy-process.json`,
`/tmp/teensy-lazy-shutdown.json`.
The full SSH test's random key conflicted with existing host trust and was
correctly rejected; known_hosts was preserved. Five repeated SSH connection
failures on the final firmware returned both heaps exactly to baseline
(324,012 internal / 8,123,868 PSRAM bytes); Telnet reports zero stack bytes.
Evidence: `/tmp/teensy-lazy-final.json`. Generated process/shutdown fixtures were
removed. Synth regression could not run
because the codec reported missing; analog audio is not validated by this work.

Other idle allocations audited, retained for now:

| Reservation | Internal RAM | Reason / next work |
| --- | ---: | --- |
| Audio playback and microphone rings | 48 KiB | Subsequently made on-demand; see the newer audio section above |
| Additional UART RX buffers | 12 KiB | Preserve receive timing; consider allocating per opened port |
| Serial capture/writer stacks | 10 KiB | Need coordinated service stop and log draining |
| Ethernet/network event stacks | 14 KiB | Shared by all network clients; not Telnet-specific |
| lwIP pools and buffers | About 50 KiB | Shared networking infrastructure; separate sizing audit needed |

Local console stacks and USB sector buffers remain available for console and
storage operation. Small Telnet metadata/TCB/buffers remain static. PSRAM still
contains existing app catalogs/job metadata; this change does not partition or
consume additional PSRAM. Allocation is on demand under the existing policies.

## Complete offline Teensy manual — 2026-10-05

The manual selection now covers 27 apps and 67 shell commands, including Synth.
Every selected page embeds its full audited text in program flash. Previously,
most entries embedded only a generic instruction to download a manual over Wi-Fi;
app overrides for WebRadio and ltop were also not applied. The default upstream
ESP manual-generation policy is unchanged.

Teensy pages describe wired Ethernet, SGTL5000 microphone/playback, the terminal
Synth/WebRadio/Calc interfaces, single-core ltop stack headroom, persistent LCD
font/colors, startup files, and `/flash/.ssh` and settings paths. Shared neutral
app controls are retained with port-specific limits. Scope documents the legacy
ADC40/AmpEn40 conflict; PD Power and OBD remain explicitly simulated. `help status`
reports the complete embedded page count. No SD/manual download is required.

Host coverage evaluates actual registry conditions for full/reduced profiles,
checks complete generated bodies and prevents stale ESP instructions. Device
procedure: `scripts/ports/test_teensy41_manual_device.py`. All 15 port and 16
shared generator tests pass, along with the new actual-code wrapped-row
navigation regression (widths 1–100, tabs, CRLF, long words and blank lines).

Installed and device-validated on the unchanged legacy wiring. All 94 pages
match the actual app/command lists and pass beginning/end text checks; bare
aliases, search, Help browser and LCD paging pass. Free memory before/after:
36,324 internal / 8,123,868 PSRAM bytes, unchanged. Evidence:
`/tmp/teensy-manual-device.json`, `/tmp/teensy-manual-upload.log`.

Acceptance uncovered two shared display bugs, now fixed: long index/search
summaries exceeded the 192-byte printf scratch buffer and swallowed the next
row's newline; summaries now stream separately. Backward pager traversal skipped
a wrapped row, causing G/End to stop short; previous-row calculation is corrected.

Installed RAM1 430,048 / RAM2 347,000 bytes (unchanged), program flash 1,468,728
bytes (+35,112 bytes vs the USB batching image). Installed HEX SHA256:
`f2d1eac065845f0147b861325173c024cf0cfbf3a126cd1f7f91122627f13bd6`.

## Faster FAT32 USB usage — 2026-10-05

Installed `teensy41_telnet_legacy`, with bench pins unchanged. USB reads now
batch up to eight sectors using a 4 KiB aligned internal OCRAM buffer. FAT scans
reuse SdFat's maintained free-cluster counter; `df --refresh` explicitly reloads
USB accounting and refuses while USB files/directories are open.

On the current 16 GB FAT32 drive, cold USB accounting (SD already warm) dropped
from 45.828 to 5.791 seconds. Warm `df`: 0.031 seconds; USB refresh: 5.823 seconds.
First whole-system `df` after boot: 11.298 seconds, including SD accounting.
ASan/UBSan batch tests, 11 manual tests, firmware build/upload and device checks
passed. Device coverage includes maintained counts vs recounts after mutations,
PSRAM read hashes, busy-handle refusal, logical remount and fixture cleanup.
See [USB details](teensy41-usb-storage.md); log `/tmp/teensy-df-device.json`.

Build RAM1: 430,048 bytes; RAM2: 347,000 bytes; flash: 1,433,616 bytes.
Net static RAM increase: 3.5 KiB; no additional PSRAM buffer. Final observed heap:
36,324 / 84,672 bytes free; PSRAM: 8,123,868 / 8,388,608 bytes free.
Installed HEX SHA256:
`3592544bd398067c1cd33448eacd57225abad3b3ebcc09abb5a6bf1727df8ca0`.

## Flash test-fixture cleanup — 2026-10-05

Removed 27 inventoried flash test directories (138 files) generated by the
flash, Files, text-app, USB-storage and Python-network suites. Before deletion,
file paths/sizes were checked against the audit; 34 non-test file hashes matched
afterward (history files excluded because commands update them). Libraries,
settings, SSH keys, startup script and user files were preserved.

Flash allocation fell from 16,187,392 to 2,555,904 bytes immediately after
cleanup. Following a complete Python-network regression and its automatic
cleanup, final allocation is 2,621,440 / 16,777,216 bytes: 2.5 MiB used and
13.5 MiB free. The former usage was dominated by test fixtures and the driver's
64 KiB filesystem blocks. No filesystem reformat or firmware change was needed.

All five suites now call one guarded cleanup helper after success; they remove
only exact generated roots from that run. `--keep-fixtures` retains successful
fixtures for inspection. Failed/interrupted tests retain evidence. Flash
`--verify-existing` preserves the supplied prior-run roots. CLI parsing for all
five suites and five cleanup guard unit tests pass. The full on-device Python
network suite passes with `--volume /flash` and confirms its new directory absent.
Evidence: `/tmp/teensy-flash-cleanup.json`,
`/tmp/teensy-fixture-cleanup-network.json`.

## Flash history, SSH repair and boot networking — 2026-10-05

Installed on `teensy41_telnet_legacy` with unchanged bench pin assignments.
The shell now saves 12 commands per LCD/USB/Telnet console under
`/flash/.shell/history-{lcd,usb,telnet}`. It reuses existing PSRAM history buffers,
adds 12 bytes of metadata per session, batches writes every 30 seconds while
console polling runs, and flushes on orderly reboot/shutdown and Telnet teardown.
Temporary-file close plus LittleFS replacement preserves the previous saved
history on failure. USB reconnect retains unsaved RAM history. Script-only
sessions do not persist. Up/Down recall survives restart. The upstream
`/.shell/history` path was on the unwritable virtual root.

History ASan/UBSan tests pass, including write/close/replace failure retention,
bounded/malformed loads and timer wrap. Device USB/LCD timed saves, isolation,
reboot recall and `poweroff --check` pass. Telnet-specific physical acceptance
remains separately listed. See [history details](teensy41-shell-composition.md).

SSH's config path likewise incorrectly resolved to `/.ssh`; it now uses
`/flash/.ssh`, matching sshkey's key location. Further testing found that calling
the entropy HAL initializer on every request could discard completed random
samples because its stopped-oscillator test also matches that state. The
Ethernet owner now initializes the generator once (and after clock disable),
preserves completed samples, and uses the driver's recovery path for latched
errors. Crypto initialization is checked explicitly; persistent entropy failures
remain bounded errors with diagnostics, never a weak-randomness fallback.
The entropy host regression covers completed-sample preservation, error recovery,
request bounds and peripheral-clock restart. Full device SSH regression passes:
password login, bidirectional/bulk I/O, editing keys, wrong-password and changed
host-key rejection, cancellation, remote close and repeated cleanup. Warm free
memory stays at 35,796 internal / 8,123,868 PSRAM across repeated connections.
The isolated server ran on the user's PC with temporary test credentials; the
real `dennis` account was not used.

Networking is enabled at boot through the existing startup mechanism:
`setterm startup flash`, with `network up` in `/flash/.shell/startup`. Neither
flash nor SD previously had a startup file. Reboot verified DHCP at
192.168.1.197 without manually starting networking. No network listener was
added to startup. A startup manager UI remains a possible future convenience.

Installed HEX SHA256:
`b3916b35b4aa95551a818b40eb6b6a3182208f1285c6a16cb402f00f69f369b6`.
Flash 1,432,568; RAM1 430,560; RAM2 342,904 bytes. Idle free memory before SSH:
35,812 internal / 8,123,868 PSRAM. Evidence: `/tmp/teensy-history-ssh-build.log`,
`/tmp/teensy-history-upload.log`, `/tmp/teensy-history-device.json`,
`/tmp/teensy-history-ssh-device.json`.

## Num Lock USB request ordering fix — 2026-10-04

User testing of the initial preference implementation found that the Num Lock
key still changed logical behavior but its light did not work; keyboard
unplug/replug was also reported failing. The initial connection hook queued
SET_REPORT while KeyboardController's SET_IDLE was still in flight. USBHIDParser
reuses one setup packet, and queue_Control_Transfer references that packet until
completion, so the requests must not overlap.

The installed correction records the connection preference, waits for the
matching SET_IDLE completion callback, then submits the LED update from host
polling. Disconnect cancels the pending request; repeated polling does not
reassert Num Lock after a manual toggle. Explicit preference changes use the
same pending-request path. Host sanitizer tests cover the ordering, cancellation,
reconnect and existing keyboard lifecycle/keypad behavior. On-device startup
reaches `numlock=on auto=on`. User confirmed the physical LED, manual toggle,
unplug/replug auto-enable, letters and keypad numbers all work on the bench keyboard.

Num Lock correction HEX SHA256:
`b167e0c8d1868ec03a8b53d92ee4ce09223c1fbc67df4a0b757be8c82e82ea09`.
Flash 1,431,616 bytes; RAM1 430,560; RAM2 342,904. Measured free memory:
35,812 internal / 8,123,892 PSRAM bytes. Evidence:
`/tmp/teensy-numlock-fix-build.log`, `/tmp/teensy-numlock-upload.log`.

## Saved keyboard Num Lock — 2026-10-04

Installed and enabled `setterm numlock on` on the unchanged legacy bench
profile. This saves `keyboard/numlock` in `/flash/.solar-settings/keyboard.bin`,
enables Num Lock immediately, and applies it once per USB keyboard connection
(including startup). The physical key remains usable while connected. `off`
disables automatic enabling without changing the current lock state. `setterm`
shows the preference and `lcd` reports current driver state plus `auto=`.
`man setterm` now includes the port's preference reference offline.

Keyboard and settings host sanitizer suites and 11 port manual tests pass.
Device checks pass for on/off, invalid-value rejection, unchanged memory, and
persistence across reboot. After reboot the Microsoft 045e:0750 driver reports
`numlock=on auto=on`. Physical LED/keypad unplug/replug confirmation is tracked
as KEY-NUM in the master checklist.

Initial Num Lock image HEX SHA256:
`a39b5d60d129b4533d743db7969a211f6d1aee19fc117cf08dd0d31a9e577747`.
Flash 1,431,536 bytes; RAM1 430,528; RAM2 342,904. Idle free memory remains
35,844 internal / 8,123,892 PSRAM bytes. Build/upload evidence:
`/tmp/teensy-numlock-build.log`, `/tmp/teensy-numlock-upload.log`.
See [keyboard preferences](teensy41-keyboard.md).

## Graceful On/Off shutdown — 2026-10-04

Installed on `teensy41_telnet_legacy`; the dedicated On/Off pad uses no GPIO.
Bench LCD CS37/reset9/WAIT15, AmpEn40 and Serial1 remain unchanged. A short
On/Off-to-GND press and `poweroff` use the same cleanup coordinator.
`poweroff --check` performs real cleanup without cutting power;
`poweroff status` reports completion/refusal. The embedded `man poweroff`
reference is available offline.

Python exposes `solaros.shutdown_requested()`, gets a two-second grace period,
then one interrupt per attempt. Both VM polling and the native file-I/O return
boundary allow a suspended worker to finish its finally/with cleanup. Jobs and
scheduler triggers pause; console owners release COM/hardware resources;
serial logs drain; mounted storage is synced. Other active/retained apps must
be closed first. A 15-second deadline, storage error or unconfirmed configured
PD controller leaves power on; no live VM is forcibly deleted. Never-configured
PD is skipped on the unwired bench. External regulator EN control and emergency
PD fallback are still hardware work. `reboot` retains its previous reset path.

Host sanitizer tests pass for the coordinator failure/deadline paths, serial
logger drain/cleanup and existing PD policy/app. Manual tests pass (16 generator,
11 port). Final-device shutdown acceptance passes for idle/refused requests,
cooperative Python, a suspended CPU loop with a three-second finally,
interrupt refusal timeout, foreground REPL, script/serial cleanup, offline help
and exact memory recovery. The existing Python process device regression also
passes (pause/bg/fg, native I/O, disconnect, cross-console reattach, input,
repeated VM cleanup). Shutdown task stack high-water free: 6,320 bytes after
these tests; real PD and sustained serial drain have not exercised this stack.
The user confirmed physical On/Off-to-GND shutdown and subsequent wake on
2026-10-04. Detailed peripheral recovery, shutdown under recording load, forced
hold and real PD/rail measurements remain in the master checklist.

Shutdown acceptance HEX SHA256:
`feb87ed27c696eaaad771b8091a46489df77b619225a0f1edb050018c5b0b48a`.
Flash 1,430,016 bytes; static RAM1 430,528; RAM2 342,904. The coordinator adds
8 KiB of internal OCRAM stack. Measured idle heap: 35,844 / 84,192 bytes internal;
PSRAM 8,123,892 / 8,388,608 bytes. Acceptance evidence:
`/tmp/teensy-shutdown-device.json`, `/tmp/teensy-shutdown-process-regression.json`,
`/tmp/teensy-power-button-upload.log`.
The device test leaves unique `/sd/_shutdown_*` fixtures for inspection.
See [shutdown implementation and limitations](teensy41-shutdown.md).

## Serial terminal/logger — 2026-10-03

Installed native shared serial capture for `com` and `serial` on the unchanged
legacy bench profile. `serial record BUS BAUD NEWFILE [--timestamp]` records in
the background; `serial stop BUS` drains/closes it. COM exit, suspension and
console disconnect leave an explicit recording running. Raw files preserve RX
bytes; timestamped logs contain elapsed-millisecond RX/TX hex chunks. Status
reports bounded queues, software drops and storage errors. Existing files are
never overwritten.

`serial config BUS BAUD FORMAT [none|xonxoff]` sets an idle port's RAM-only
framing/flow preferences. Supports 8N1/8N2, 7E1/7E2/7O1/7O2 and
8E1/8E2/8O1/8O2. COM adds `--baud`, `--enter cr|lf|crlf` and retains `--hex`.
RTS/CTS, USB-host serial adapters and unsupported framing combinations are not
implemented. Python/MIDI/raw UART still use their existing framing contracts.

Each log allocates 64 KiB PSRAM; each terminal allocates a separate 4 KiB view
queue. Two service tasks use 4/6 KiB internal OCRAM stacks. Idle internal heap is
36,100 bytes, PSRAM 8,123,928 bytes, with exact recovery after repeated acceptance
cycles. On-board task high-water marks after the test were 3,076 bytes for
capture and 4,104 for the writer; this is not a sustained-throughput result.

Host sanitizer tests cover binary fidelity, overflow/wrap, simultaneous ports,
framing validation, XON/XOFF including final XON with a full TX queue, partial
TX, failed storage, ownership and cleanup. The existing hardware/COM device
regression also passes.
Device acceptance verifies CR/LF/CRLF TX logs, configuration, suspend/close,
disconnect, duplicate-file refusal, and exact memory recovery. The first device
run exposed the adapter's 128-byte read limit; corrected capture uses bounded
128-byte reads, now enforced by the host fixture. External framing, RX fidelity,
flow-control peers and sustained load remain in the master checklist.

See `test_teensy41_serial_terminal.py`, `test_teensy41_serial_terminal_host.sh`
and [hardware notes](teensy41-hardware-resources.md). The port does not measure
hardware UART overrun/parity/framing errors; zero software drops is not a
lossless-capture guarantee.

## Python hardware/offline bundle — 2026-10-03

Implemented native resource-managed handles plus a `machine` compatibility
subset for GPIO, I2C, expansion SPI, UART, ADC and PWM. The installed `os`/`time`
helpers cover file/directory operations, UTC seconds and cooperative sleeps/ticks.
Imports search `/flash/lib` and `/sd/lib`. The VM still uses 512 KiB PSRAM;
16 bounded native hardware handles cost 576 additional static RAM1 bytes.
Before the serial service, measured idle heap was 37,572 bytes, with 8,123,928 bytes free PSRAM.

Installed 71,061 bytes of versioned libraries/license/API notes in `/flash/lib`,
plus examples in `/sd/python-examples`. Eleven Python modules include the port
compatibility files, heapq/bisect/itertools/functools/context helpers and two
import-tested IMU drivers (BMM150/LSM9DS1). `manifest.json` records sources and
hashes. Full official micropython-lib source at commit
`4fa59bd6a5916783e8503e9f2339627c8cffa5bf` is cached as
`/sd/python-offline/micropython-lib-4fa59bd6a591.zip` (793,639 bytes), verified
SHA256 `4e108a708be3808a3745c87dab8af5ffeed39ea43950df6c27d5320b99b109a9`.
Archive-only packages have not been certified compatible.

Host wrapper and manual tests pass. Device acceptance covers imports, GPIO/UART
and paired PWM conflicts, reserved pins/I2C address, transfer/handle limits,
filesystem operations and directory validation, tick wrapping, and repeated,
exception and Ctrl+C cleanup with exact memory recovery. See
`test_teensy41_python_hardware.py`. Physical sensor/SPI/ADC/PWM validation is
pending in the master checklist. `README.txt` in the library directory documents
all limits. MIDI/audio/USB-PD/CAN Python bindings are still future work; this is
not complete stock `machine`/standard-library compatibility.

The actual legacy bench uses LCD CS37/reset9/WAIT15: SPI slot0 and slot2 are
blocked, slot1 CS36 is available, and ADC pin14 is unreserved. PWM claims the
whole 28/29 or 36/37 timer pair, so only 28/29 is usable with this LCD profile.
ADC is disabled in profiles with physical scope ADC enabled until ADC controller
arbitration is added.

## Pipe pager — 2026-10-03

`commands | less` now opens the interactive pager on USB/LCD. Other supported
producers and filters can also end in `| less`. Input remains bounded to 8 KiB
in PSRAM; `less` must end the command line. No temporary files or extra tasks.
Host sanitizer checks cover exact/overflow input, syntax rejection and buffer
release. `test_teensy41_less_pipe.py` passed on the installed legacy profile:
LCD scrolling, USB quit, empty input, cross-console ownership rejection and
five repeated cycles with exact heap/PSRAM recovery. Static RAM use is unchanged.

## Upstream 4.15.18 integration — 2026-10-03

Merged upstream `3b4cf28a` after local checkpoint `12593dac`. The checkpoint
firmware and source backup are in the sibling `solar_os-baselines` directory.
Teensy adapters and the legacy bench wiring remain in use.

- Memory: measured idle internal free heap increased from 24,388 to 38,148 bytes
  (+13,760); free PSRAM is 8,123,928 bytes. Keep one 8 MiB chip, the existing
  system reserve and bounded pipe policy. Up to three external workers allocate
  PSRAM stacks on demand; critical tasks retain internal stacks. The linker still
  requires 72 KiB startup headroom.
- `ltop`: interactive interval CPU/task monitor; FREE is stack high-water headroom
  in bytes on Teensy. Repeated LCD launch/exit recovered memory exactly.
- `webradio`: shared station catalog/TUI and a 128 KiB PSRAM PCM ring. Catalog and
  UI lifecycle pass on hardware; real streaming/audio awaits the absent SGTL5000.
- `sshkey status|pub|gen|rm`: persistent RSA identity under `/flash/.ssh`.
  Offline 2048-bit generation, public export, overwrite refusal and removal pass.
  Entropy initialization now works before Ethernet starts.
- `midi record usb|slotN NEWFILE.smr` and `midi play FILE.smr usb|slotN` use a
  transport-independent timestamped SMR1 format. USB cable 0 and short MIDI events
  are supported; SysEx and standard MIDI file import/export are not implemented.
  UART slots use 31,250 baud and existing ownership claims. Record cancellation
  passes; physical USB/DIN routing and timing await connected MIDI hardware.
- `pd open i2cN ADDRESS BOARD_MAX_MV BOARD_MAX_MA`, `pd status`,
  `pd request MV MA`, `pd close`: STUSB4500 volatile fixed-PDO control with board
  limits and confirmed negotiation state. Hardware is explicitly not connected;
  nothing opens automatically. Close stops monitoring, not power delivery.
  Existing I2C/SPI/UART tools remain available with resource ownership.

Host sanitizer tests cover the PD register/state model, MIDI codec/record format,
radio PCM backpressure and cleanup, plus composition, synth, images, graphics,
settings, clock and hardware services. Device acceptance covers key commands,
monitor/radio cleanup, MIDI recording cancellation and LCD appearance. The existing
composition regression passed USB/LCD pipelines, 20-cycle memory recovery and
7 MiB PSRAM pressure, but its host NTP fixture timed out; this network check needs
rerunning. Physical audio, MIDI and PD checks are tracked in the
[needs-testing checklist](teensy41-test-checklist.md), including PD ALERT latency.

## Revised PCB target — separate from the bench

Dennis supplied a revised PCB pinout on 2026-10-01 and confirmed it is not for
the connected test system. See [PCB assignments and open questions](superkeyboard-pcb-pinout.md).
RGB0/AmpEn1/DMM40 supersede the earlier proposed PCB AmpEn0 assignment. Do not
flash a changed profile. The flash-drive `Pins_v3.ods` was inspected directly:
header 2 is main-display-only, pin 9 is backlight PWM (`slot2CS` is a connector
reference), and pin 33 is secondary backlight; its extra Motor mark is stale,
not intentional USB-C sharing. Display-specific CS wiring and several schematic
labels still need checking before a PCB profile. Bench wiring remains as below.

## Latest addition: LCD appearance — 2026-10-03

`lcd font 1|2|3` selects 100×30, 50×15, or 33×10 cells. `lcd color FG BG`
sets default ANSI colors; `lcd colors` lists names and `lcd reset` restores
1× white on black. Preferences persist in the `lcd_terminal` settings namespace.
Resize clears the text console and updates shell/app geometry. Changes refuse
active/retained LCD apps or active graphics. The fixed PSRAM cell buffer is reused;
build-reported RAM1/RAM2 are unchanged. Apps' explicit ANSI colors are preserved.

Host ANSI tests cover all scales, malformed input, wrapping, and default colors.
`scripts/ports/test_teensy41_lcd_appearance.py` passed on hardware: all sizes,
color validation, busy-app rejection, local-shell resize, reboot persistence,
and restoration of defaults. Physical pixel appearance was not visually inspected.
Hardware log: `/tmp/teensy-lcd-appearance-test.log`. Board left at 1× white on black.

## PSRAM policy and shell composition — 2026-10-03

Single-PSRAM allocation policy and bounded shell composition are implemented;
see [feature notes and acceptance steps](teensy41-shell-composition.md). The
background-script stack uses 16 KiB EXTMEM and settings snapshots use external
system allocations. `;`, audited `&&`, and 8 KiB bounded pipes are enabled in
workstation builds. Build, host tests, USB/LCD pipeline acceptance, 7 MiB RAMFS
pressure, real NTP status/cancellation, background job stack execution, exact
memory recovery and reboot persistence pass. The legacy profile is installed.

## Installed firmware and wiring

Installed **`teensy41_telnet_legacy`**, with **AmpEn on pin 40**, Serial1 retained
and physical scope ADC disabled. Keep this profile until the AmpEn0/ADC40 move.
The normal `teensy41_display` image requires that wiring change and disables
Serial1. Both scope and USB-PD demo apps are available on the installed image.

Installed HEX SHA256:
`348f539ff2eb5f6d26378b06f033a3e1405ec4f13333df8cae92adb9cc519f99`.
Flash 1,425,392 bytes; RAM1 430,272; RAM2 334,712.
Microsoft keyboard `045e:0750`, powered USB host hub, RA8875 LCD, native SD,
USB drive, QSPI flash and 8 MiB PSRAM. Audio shield was absent in recent tests.
See [quick-start and wiring](README.md).

## Latest addition: lightweight Python highlighting

Installed in `edit`: Python keywords, strings, comments, numbers, built-ins,
definition names and constants have distinct colors. Shared TUI foreground
attributes support the LCD and serial terminal emulators, with monochrome
attribute fallback. Selection keeps default foreground/inverse contrast.

The shared lexer has compact line-start checkpoints and the editor caches
visible row styles. Edits propagate state in bounded batches and stop at a
matching unchanged suffix; cursor-only redraws reuse cached work. See
[syntax limits and tests](teensy41-syntax.md) and
[demo source](../../examples/teensy41/syntax_demo.py).

Host ASan/UBSan passes 4,000 randomized edit comparisons plus large-file/budget
checks; five editor key-policy tests pass. Device color/selection/multiline/
resume/exit tests pass, as do LCD text rendering and exact five-cycle cleanup.
Evidence: `/tmp/teensy-syntax-device.json`. Test editors are closed and temporary
RAMFS removed. Physical LCD color readability remains a manual check.

Recovery checkpoint: `../solar_os-baselines/2026-10-01-syntax/`.
Syntax highlighting is included in the commit accompanying this handover.
RAMFS and earlier workstation work are committed/pushed (`9435980`, `d6a16f9`).
Unrelated DNP3 work is untouched. The installed firmware remains the tested
image identified above; the expanded editor manual text is a documentation
update for the next firmware build.

## Previous addition: PSRAM RAMFS (original item 7)

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
`2026-10-01-syntax/` is the installed editor-highlighting build;
`2026-10-01-ramfs/` is the preceding RAMFS build;
`2026-09-30-hardware/` is the prior hardware-command build;
`2026-09-30-completion/` is the earlier completion build;
`2026-09-30-netdiag/` is the prior diagnostic build;
`2026-09-30-process-jobs/`, `2026-09-30-jobs/`, and `2026-09-30-sessions/`
are earlier workstation checkpoints;
`2026-09-29-keyboard/` is the previous keyboard build;
`2026-09-29-workstation/` is its predecessor. These artifacts are not committed.
The keyboard snapshot predates the final user confirmation; current docs record it.

The prior integrated port checkpoint is committed/pushed as `d51761a` on
`teensy41`. Stages 1/2/2b/3/4 are committed as `d6a16f9`, and RAMFS as
`9435980`. Unrelated unfinished `solar_os_dnp3_bridge.*` and `src/vendor/opendnp3/`
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

### Approved final-PCB CTS assignment — 2026-10-03

User approved the RS232 module routing change in
`PCBPlacement_2026-10-03/modules/RS232`: header 7 / GPIO37 is CTS,
header 11 / GPIO25 remains RTS, and DTR moves to freed header 12 / GPIO24.
The motherboard pin mapping stays unchanged; schematic and unrouted module PCB
pad nets were synchronized and validated. The slot-1 equivalent header 7 maps
to GPIO36, but no slot-1 module redesign is included in this change.

**Bench exception:** retain the installed `teensy41_telnet_legacy` profile and
GPIO37 LCD CS. This is a final-PCB assignment, not authorization to claim that
pin for CTS on the bench. Firmware still supports none/XON-XOFF only. Final
RTS/CTS integration must resolve XBAR polarity, arbitrate the shared I2C2 bus
and the CS pin, and pass physical jumper/flow tests. See PCB review notes and
the master checklist. No firmware rebuild/upload accompanied this change.
