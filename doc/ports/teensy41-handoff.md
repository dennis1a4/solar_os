# Teensy / SuperKeyboard handover

Updated 2026-10-03. Branch: `teensy41` (upstream integration retained as `teensy41-upstream-4.15.18`); GitHub: `dennis1a4/solar_os`.
This is the current state. Older snapshots are in the
[handover history](teensy41-handoff-history.md).

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
