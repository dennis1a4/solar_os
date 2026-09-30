# Teensy handover history

Historical snapshots through 2026-09-29. Statements about installed firmware,
uncommitted changes and next actions describe their original dates. Use the
[current handover](teensy41-handoff.md) for present state.

# Keyboard repeat installed — 2026-09-29

Uploaded `teensy41_telnet_legacy` with AmpEn40 retained. Keyboard repeat now
starts after 400 ms and runs every 33 ms. See [keyboard notes](teensy41-keyboard.md)
for modifier, multiple-key and app-transition policy. Host sanitizer and child
lifecycle and dual-console device tests pass. User confirmed physical repeat and
release for letters, Left, Backspace and Shift changes. Physical hotplug and
broader app/multiple-key checks remain pending.
The attached Microsoft keyboard enumerates as `045e:0750`; `lcd` exposes repeat
counters. Flash 1,340,592 bytes; RAM1 436,160; RAM2 271,192.
Checkpoint: `../solar_os-baselines/2026-09-29-keyboard/`.

# Workstation commands installed — 2026-09-29

The user requested a more Unix-like workstation shell. See the
[command audit](teensy41-workstation.md) for the full comparison and remaining
service dependencies. Website manual version is 4.15.4; this source is 4.13.2.

Uploaded and device-tested **teensy41_telnet_legacy**, retaining **AmpEn40**,
Serial1 and disabled physical scope ADC. No rewiring. New commands: embedded
Help/Man browser/search, Watch, version/board/status/pwd, task Top, Port listing,
DF, local Date/Time, ZIP/Unzip and Ethernet Curl. Session/Sessions now inspect
fixed consoles; **retained sessions, fg/close and dynamic creation remain pending**.
Hexedit was already available; its manual is now included.

The workstation suite passed manual search/paging, watch timer delivery and
q/Escape/Ctrl+C/Ctrl+] exits, LCD/USB responsiveness, diagnostics, ZIP byte round
trip, HTTP text/binary hashes, curl cancellation and LCD/USB output isolation.
Repeated manual/curl cycles and delayed-peer cancellation recovered exactly
30,692 internal heap bytes and 8,202,420 PSRAM bytes.
Dual-console regression also passed ownership, cancellation, wait, repeated
cleanup and USB reconnect (`/tmp/teensy-workstation-display-regression.json`).
Logs: `/tmp/teensy-workstation-device-final.json`,
`/tmp/teensy-workstation-telnet.json`. Telnet regression also passed remote
man/watch/session, authentication, live resize, reconnect and restart cleanup.

First `df` scan on the attached media took 51.36 seconds, versus 0.06 seconds
cached. The initial scan is synchronous and can delay both consoles. RTC read
as 2018 after restart; no clock/timezone change was made. HTTPS needs correct
UTC time (`rtc set ...`); public HTTPS is not newly validated by the local HTTP test.

Host manual (14 shared + 6 port tests), Clock/date/time and child lifecycle tests
pass. Legacy, normal display and USB-only apps profiles build. Only the legacy
image was uploaded; normal display still requires the AmpEn0/ADC40 move.
Installed HEX SHA256:
`b285ded25db64995fff4914a81bd122773798bc433f88eceed79f7a2def1767d`.
Flash 1,337,688 bytes; RAM1 435,616; RAM2 271,192. RAM1 unchanged.

After tests: Ethernet up, Telnet listener stopped, test password removed,
media mounted. Existing user files/password file preserved. Unique workstation
SD fixture folders remain for inspection. No commit/push; preserve unrelated DNP3.
Checkpoint: `../solar_os-baselines/2026-09-29-workstation/`.
The older pause/unplug instructions below describe historical state.

# Paused and ready to unplug — 2026-09-28

The user confirmed Telnet worked. Wrap-up over USB serial confirmed Telnet
stopped with no client, Ethernet stopped (DHCP off), SD safely ejected with zero
open handles, and USB storage safely ejected. The board remains powered by USB;
it is ready to unplug. No firmware changes or uploads during this cleanup.
Existing user files, including any Telnet password file, were preserved.

On resuming, use the installed `teensy41_telnet_legacy` wiring: AmpEn40,
physical scope ADC disabled, Serial1 enabled. Do not flash the normal display
candidate until the AmpEn0/ADC40 wiring move is complete. If resuming without a
power cycle, run `sd mount` and `usb mount` to remount the ejected media.
For Telnet, run `network up`, check `network status` for the current DHCP address,
then `telnetd start /flash/telnet.pass` with the existing password file.

USB monitor: `pio device monitor --port /dev/ttyACM0 --baud 115200 --raw --exit-char 28`.
Exit with **Ctrl+\** (Control and backslash), which releases the port for automation.

Validated source/firmware/test checkpoint:
`../solar_os-baselines/2026-09-28-telnet-validated/` (predates this documentation
wrap-up). See the [master checklist](teensy41-test-checklist.md) for outstanding
work, including implementing keyboard repeat before testing held keys, physical
SD recovery checks, Clock LCD/audio checks, and hardware bring-up for scope,
USB-PD and CAN. Working-tree changes remain uncommitted.

# Telnet installed on current wiring — 2026-09-28

`teensy41_telnet_legacy` uploaded and real Ethernet/USB tests pass. AmpEn remains
pin 40, scope ADC is disabled, and Serial1 is retained. Scope/pdpower demos are
now installed. Normal `teensy41_display` still requires the AmpEn0/ADC40 move.
The user confirmed their Telnet connection worked after the automated tests.
Last observed DHCP address: 192.168.1.197.
Start with `telnetd start /flash/telnet.pass` after creating a one-line password
file. See [Telnet usage/evidence](teensy41-telnetd.md) and the
[master checklist](teensy41-test-checklist.md).

Ten reconnects recovered exactly to warm idle: internal 30,720 bytes free,
PSRAM 8,202,480 bytes free. Wrong login, live NAWS, independent shell, busy peer,
Files/Python disconnect cleanup, exit, network restart and immediate server
restarts passed. Test password file removed; no credentials saved in source.

The dated entries below describe earlier checkpoints; this entry is the current
installed-firmware status. Earlier wiring/test evidence remains useful.

# Scope candidate and pending pin move — 2026-09-28

**Testing:** [Master outstanding test checklist](teensy41-test-checklist.md).

Native graphical `scope` / `scope --demo` added with triggered ADC snapshots,
measurements and manual positive-only range scaling. Host model/app tests and
final display build pass (flash 1,279,612 bytes; RAM1 435,232; RAM2 227,904).
The candidate maps ADC to pin 40 and AmpEn to pin 0, disabling Serial1 UART RX
on pin 0; USB and LCD consoles remain. **Not flashed: physical AmpEn move is
pending and the installed legacy firmware still drives pin 40.** ADC/DMA hardware validation
remains pending. See [scope notes](teensy41-scope.md).

# USB-PD power app candidate — 2026-09-28

Native `pdpower --demo` supports voltage/current selection and simulated negotiation
on LCD or USB text terminals. Host model/app sanitizer tests and display build
pass (+5,016 flash bytes, unchanged static RAM). Hardware
STUSB4500 backend, I2C wiring and PCB power limits remain pending; motor control
is deferred. No I2C/NVM writes or real voltage changes. Demo now installed with Telnet;
its device-specific interaction tests remain pending. See [power notes](teensy41-power.md).

# Clock/timezone installed — 2026-09-28

Shared graphical `clock`, `clock -s`, and `clock -a MM:SS` enabled with the Teensy
RTC/timezone and transient countdown adapters. `setterm timezone Manitoba`
selects persistent fixed UTC-5 without seasonal changes. RTC remains UTC.
Host tests and remote device checks pass, including stopwatch/countdown, repeated
launch memory recovery and timezone/RTC persistence across software reboot.
Flashed 2026-09-28 with SD recovery and OBD demo included. Visual inspection is
pending; audio shield reports missing. See [clock port notes](teensy41-clock.md)
for commands, evidence and memory. Earlier USB/keyboard physical checks used
the old USB baseline; historical checkpoints remain preserved.

# OBD demo candidate — 2026-09-28

Native `obd --demo` now scans two simulated ECUs through CAN/ISO-TP, displays
readiness and stored/pending/permanent codes, confirms selected-ECU clear with
rescan, and saves exclusively created reports. Protocol and real app lifecycle
host tests pass with ASan/UBSan; installed with the clock image. No CAN
hardware tested, canmon not implemented yet. See [OBD notes](teensy41-obd.md)
for scenarios, limits and the pre-existing Python state-policy failures.
PCB allocation: display slot 0; CAN A/B expansion slots 1/2. The temporary pin
conflicts are expected to be fixed by the PCB; current wiring is unchanged.

# SD recovery candidate — 2026-09-28

Implemented SD removal detection, guarded volume access, stale-handle remount
blocking and `sd status|mount|eject`. Display and network builds pass, as do
10,000 sanitized lifecycle cycles, seven automation protocol tests and 14 manual
tests. Installed with the clock image; boot mounting passed before and after
software reboot. Physical tests wait for the operator.
See [implementation and guided runner](teensy41-sd-recovery.md). Historical
USB baseline checkpoint remains unchanged.

# USB flash-drive integration — 2026-09-28

The user is currently away from the device, working remotely over SSH. Physical
USB and keyboard disconnect tests passed on the previous USB baseline, except
that keyboard repeat is missing. SD recovery is now installed; physical SD tests
are pending. See the
[physical reconnect checklist](teensy41-hotplug-tests.md). No USB test handles
remain open, and no physical test has been marked passed without observation.

`teensy41_display` now mounts one USB mass-storage drive's first supported
partition at `/usb`, alongside `/sd` and `/flash`. `usb [status|mount|eject]`
provides status and safe removal. See [USB storage notes](teensy41-usb-storage.md)
for limits, the cached-stack DMA fix, and reproduction commands.

Validated with the user's 16 GB vfat/FAT32 drive (15,260 MiB) and keyboard on a
powered hub. The user confirmed the keyboard stays responsive with the drive
inserted. USB file operations, native copy/rename/delete, SD/flash interoperability,
three eject/remount cycles, LCD-side binary reads and busy-handle eject refusal
passed in `/tmp/teensy-usb-test-final.json`. The existing SD/flash regression and
empty-host tests passed; both display and non-USB network builds passed. The
host check verifies 10 linked USB buffers/driver symbols reside in uncached
DTCM. All 14 manual tests passed.

Initial testing exposed the bundled MSC driver's uncached-stack assumption and
unbounded transfer waits. A generated build-only source adaptation and a DTCM
sector bounce buffer fix these without editing the installed SDK. Do not remove
this adaptation while the LCD task stack lives in cached OCRAM. Power draw was
not measured, so do not attribute the initial freeze solely to the unpowered hub.

Files root listing shows `/usb` in both panes. Physical unexpected-removal
validation remains pending; the temporary read-only handle was closed and both
consoles are back at their shells. USB remains mounted, zero open handles.
Recovery checkpoint: `../solar_os-baselines/2026-09-28-usb-storage/`.
Current firmware SHA256:
`e92225d822af508e62960e44b8320900a766583dd451b37cd5638f07f5f2f6cb`.
Flash 1,234,564 bytes; RAM1 432,576 bytes; RAM2 225,720 bytes.
The earlier MQTT and graphics recovery checkpoints are preserved. No commit or
push has been made; preserve unrelated DNP3 files.

# Apps listing formatting — 2026-09-28

The Teensy `apps` command now uses `solar_os_shell_io_write_bold` for app
names, restoring normal text before each description, matching `help` and
the upstream apps listing. `teensy41_display` build and upload passed;
logs: `/tmp/teensy-apps-bold-build.log`, `/tmp/teensy-apps-bold-upload.log`.
Apps-formatting firmware SHA256:
`59e03233bf5d6dd9f253cd1ab096852a6eecbca9946fa99c9b7d0c0de6dc4548`.
Flash 1,220,760 bytes; RAM1/RAM2 unchanged. The MQTT recovery checkpoint below
is preserved. Visual confirmation of this formatting change is pending.

# MQTT Explorer checkpoint — 2026-09-28

Native `mqttx` is integrated into `teensy41_display`. See
[MQTT Explorer notes](teensy41-mqtt-explorer.md) for controls, architecture,
limits, credentials-file format and reproduction commands. It uses the shared
managed TCP session service with a background MQTT 3.1.1 subscriber, bounded
PSRAM tree/history and optional exclusive-create JSON-lines logging.

Host codec/model ASAN/UBSAN tests passed. The final isolated hardware suite
`/tmp/teensy-mqtt-fixture-final.json` passed subscription framing, QoS 1 ACK,
retained/binary/empty/oversized payloads, tree/history/hex/JSON/filter controls,
capture during display pause, a 300-message burst, automatic reconnect,
keepalive and repeated lifecycle cleanup. All 305 messages were verified in the
SD log, with zero log omissions. Internal heap and PSRAM returned exactly to
36,964 / 8,202,576 free bytes. SD fixture: `/sd/mqtt-test-777b2b25.jsonl`.
The app's ring deliberately evicted 49 history records; the tree reached its
256-node limit with 52 unindexed messages, and the 4 KiB payload was explicitly
truncated to its 2 KiB captured prefix. These are accounted-for capacity limits.

Added standard `fopen("wx")` support to the Teensy storage adapter and tested
that an existing log cannot be overwritten. LCD diagnostic key injection now
also supports arrows, Home/End, Tab, Space and Enter for repeatable UI tests.
All 14 manual-generator tests passed. Two-console regression passed in
`/tmp/teensy-mqtt-display-regression.json` (isolation, ownership, cancellation,
restart memory and USB reconnect). Read-only authenticated connection to the
user's LAN broker passed in `/tmp/teensy-mqtt-live.json`: 98 received messages,
153 tree nodes, zero truncation/unindexed messages/gaps at the observation.
The auth file's path is in that private runtime log; credentials are not in
source or test logs. The explorer is left running on the LCD. Automated keyboard
input has stopped. The user confirmed that the live LCD is readable, topic
selection works, and Tab switches to message history correctly. This completes
validation of the initial MQTT Explorer release on the Teensy setup. Do not
inject commands while the user is operating the app.

Saved checkpoint: `../solar_os-baselines/2026-09-28-mqtt-explorer/` with firmware,
ELF, source, patch, checksums and passing validation logs. It contains no broker
auth file. The initial release supports MQTT 3.1.1 TCP only, with documented
capacity limits; TLS/MQTT 5/publishing are follow-ups.

MQTT checkpoint firmware SHA256:
`df19837a96171293f1033647c3e805449d19dd20f7af602c3b4b7730171b8b3f`.
Flash 1,220,752 bytes; RAM1 428,256 bytes; RAM2 225,720 bytes.
Prior graphics firmware remains in `../solar_os-baselines/2026-09-28-graphics/`.
Preserve unrelated untracked DNP3 files. No commit or push has been made.

---

# Validated Python graphics and View checkpoint — 2026-09-28

Graphics/View work resumed on branch `teensy41`. The presenter now compares
8x8 dirty tile content against the shared canvas hashes. Palette changes,
re-entry and snapshot invalidation repaint correctly; malformed surface bounds
and unsupported rotation are rejected. Failed SPI lock acquisition is reported
and invalidates the cache so the next frame repaints.

Hardware benchmark: full frame 1,739 ms; identical redraw 33 ms; moved 8x8
rectangle 36 ms. A sampled Invaders frame took 33 ms. Full-frame transfers still
cost about 1.73 seconds at the unchanged 4 MHz SPI clock.

Automated graphics acceptance passed in `/tmp/teensy-graphics-final.json`:
PNG/JPEG fit/actual display, corrupt-image handling, USB display ownership,
Python drawing/error cleanup and Q controls, native Invaders animation/fire/exit,
Plot, and Files-to-View child return. Five additional Python/Invaders cycles
recovered exactly 36,616 internal heap bytes and 8,202,324 PSRAM bytes.
Final presentation timings were 1,739 / 33 / 36 ms (full / identical / moved
rectangle). Test demos remain at `/sd/graphics-demo-edffbdc3/`.

Official Playground Mandelbrot installation, full 480-row rendering, responsive
USB during calculation and Q-to-shell cleanup passed in
`/tmp/teensy-graphics-mandelbrot.json`. Counter 306 -> 788 confirms 482 presents
(initial clear + 480 rows + final frame). The recorded delta excludes the
initial frame; the current test records all 482 and allows 600 seconds for
this several-minute calculation. An earlier expanded test log
`teensy-graphics-complete.json` stopped at an unloaded-catalog prerequisite;
the final core log and separate Mandelbrot log supersede it. Refresh is an
interactive Playground operation; the harness now handles that explicitly.

Two-console regression passed in `/tmp/teensy-graphics-display-regression.json`:
independent output/directories, app ownership, Python cancellation, concurrent
wait, ten restart cycles with stable memory, and USB reconnect. Automated serial
tests have stopped. The user confirmed readable text, correct folder icon and
clean shapes in the Python demo, plus working Left/Right movement and Space
firing in Invaders. Python graphics and View validation is complete for the
current RA8875 display setup and supported feature scope. Do not inject local
commands while the user is operating the keyboard.

Reproducible backup: `../solar_os-baselines/2026-09-28-graphics/` contains
firmware/ELF, checksums, source snapshot, change diff and validation logs.
Changes are not committed or pushed. WebP/Lua, secondary graphics displays,
other upstream Python namespaces and combined audio/display checks remain out
of scope; the SGTL5000 shield is still disconnected.

Host presenter differential/bounds/transport tests, PNG/JPEG decoder tests,
and child lifecycle tests passed ASAN/UBSAN. All 14 manual-generator tests pass.
Firmware SHA256: `de2852cc2a59720b77f55db4f58c4da03645b5d643f26905b4d1cca21bc67ca4`.
Flash 1,203,568 bytes; RAM1 428,256 bytes; RAM2 225,704 bytes.
Preserve unrelated untracked DNP3 work.

---

# Historical paused graphics checkpoint — 2026-09-27

This section records the previous overnight pause and is superseded by the
2026-09-28 work above. See [graphics notes](teensy41-graphics.md). Current branch is
`teensy41`; preserve the unrelated untracked DNP3 work.

## Current work in progress

- Uploaded display firmware adds the shared PSRAM INDEX8/u8g2 renderer and
  fonts/icons, native View/Invaders, and synchronous Python `solaros.gfx`.
- User visually confirmed seeing the color test patterns. PNG/JPEG hardware
  viewing and fit/actual transitions ran. Python drawing and exception cleanup
  checks were reached; Invaders was animating when the user requested pause.
- **The full hardware test was intentionally interrupted, not passed.**
  Log: `/tmp/teensy-graphics.json`; last completed frame count 15. Do not report
  full graphics validation complete. The interrupted test is saved with the
  checkpoint. Host PNG/JPEG ASAN/UBSAN tests and 14 manual tests passed.
- Full-screen presentation is currently **about 1.73 seconds**. This is too slow
  for smooth games. Next work should optimize presentation before calling the
  graphical-app port complete. The shared canvas already supplies dirty tiles
  and 6,000 presented-hash slots: compare tile content (and palette changes) so
  apps which clear/redraw a frame do not force retransmission of unchanged
  tiles. No hash optimization has been implemented yet. Validate bounds and
  palette/rotation/dirty-map assumptions in the new surface adapter as well.
- Current SD demos: `/sd/graphics-demo-acaf29fc/color-bars.png`, `color-bars.jpg`,
  `drawing.py`, `broken.png`. Earlier interrupted test folders
  `/sd/graphics-demo-fc0cc8cd` and others may remain. Do not delete user files.
- The old uploader failed at the 1 MiB HEX boundary. Pinned PJRC CLI 2.3 revision
  `03fca4156c244c7ad36bd368cf6e24531dbd566a` fixed this. Normal Linux PlatformIO
  upload now calls `scripts/ports/upload_teensy41.py`, builds the tool in
  `.pio/teensy-tools/loader`, and explicitly selects `TEENSY41`. This path was
  successfully tested. Host prerequisites: make, C compiler, libusb-compat.
- Current firmware SHA256:
  `bb46b976d2b55f5d8a5e3a014d263d740c3f7a221264938f2c745f24476bd08d`.
  Flash 1,202,984 bytes. Backup firmware, ELF, source and logs:
  `../solar_os-baselines/2026-09-27-graphics-wip/`.
- View/gfx diagnostic logs are suppressed by default to preserve USB prompt
  isolation (`SK_GFX_DIAGNOSTICS=1` opts in). WebP and Lua are unavailable.
  Python graphics bindings exist; other upstream Python modules are not all
  ported. Native Invaders has no audio here (shield remains disconnected).
- The hardware test transfers images through Python REPL; shell command lines
  are too short for large hex payloads. It polls frame counts because display
  presentation yields to USB before the frame finishes. Remaining: finish
  native/Python controls and memory tests, exercise official Playground
  Mandelbrot, Files-to-View child return, Plot and dual-console regressions,
  and obtain visual confirmation of text/icons/game controls.
- Official Mandelbrot package inspected at `/tmp/solaros-mandelbrot.sopkg`;
  SHA256 matches catalog `43fc83c5448955b418641221ee3b748ceccd8f062add9ba52159cf3d60f9b6f0`.
  Uses only the supplied graphics/should_exit/getch API. Not yet run on device.

The sections below preserve the previous completed Plot/Playground baseline;
they do not describe the newly uploaded graphics checkpoint.

---

# Teensy 4.1 handoff — 2026-09-27

Start with the [Teensy README](README.md). Repository:
`/home/dennis/Documents/SuperKeyboard/Code/solar_os`, branch `teensy41`, tracking `origin/teensy41`.
The public fork is https://github.com/dennis1a4/solar_os; `upstream` points to
https://github.com/nilseuropa/solar_os. The previous synth/display checkpoint
was pushed as `f28a885` before the Plot/Playground work. Preserve unfinished, untracked DNP3 sources;
they are not included in the tested profiles or this checkpoint.

## Installed and confirmed

Profile `teensy41_display`: independent LCD/USB-host-keyboard and USB CDC
SolarOS shells, extending the existing apps/synth profile. Temporary Adafruit
RA8875 wiring: MOSI 11, MISO 12, SCK 13, CS 37, RESET 9; preset
`Adafruit_800x480`, 100x30 text terminal. User confirmed readable text, typing,
Files navigation and overall operation. Microsoft keyboard 045e:0750 works;
the initial home-built keyboard did not enumerate. Touch is out of scope.

The user removed the SGTL5000 shield to access the USB host connection.
`SGTL5000=missing` is expected; audio hardware regression cannot run until
it is reconnected. The synth was separately built, uploaded and tested before
that removal. No audible headphone listening check was recorded.

Previous display-only integration baseline (before Plot/Playground):
SHA256: `9156e51ca7cb3d1b4bbef390c185495807a6f3e10f5e640e62c61732f0281667`.
Flash 906,952 bytes, RAM1 440,224 bytes, RAM2 225,680 bytes.
Permanent baseline: `../solar_os-baselines/2026-09-27-display/`.
Prior LCD-only, synth, apps, Files and SSH backups remain in that directory tree.

## Validation

- `/tmp/teensy-display.json`: independent output/directories, singleton app
  conflict, local Python loop with responsive USB, owner-specific cancellation,
  concurrent wait, ten calc restart cycles with stable heap/PSRAM, and USB
  reconnect preserving LCD state.
- `/tmp/teensy-display-files.json`: Files SD/flash copy/move, recursive copy,
  ZIP, editor/Python child return and cleanup.
- `/tmp/teensy-display-keyboard.json`: Microsoft keyboard enumeration; physical
  typing and Files navigation subsequently confirmed by the user.
- Host ANSI terminal UBSAN, synth-engine ASAN/UBSAN, child lifecycle ASAN/UBSAN
  and 14 manual-generator tests pass. The previous USB-only synth profile builds.
- Automated two-session/Files checks preceded the final HID parser additions;
  the final firmware passed enumeration and physical keyboard/display checks.

The [display notes](teensy41-display.md) and [synth notes](teensy41-synth.md)
record architecture, reproduction and limitations. Serial test processes have
exited. Do not inject LCD commands while the local operator is typing.

## Plot/Playground checkpoint

The current `teensy41_display` build adds Plot's RA8875 drawing backend, an
`uptime` stream, Playground with verified HTTPS and persistent catalog settings,
and `rtc` / `rtc set <UTC Unix seconds>`. The user plans a coin-cell RTC backup;
battery retention has not been physically tested. Store UTC in the RTC.
PSRAM remains the preferred large application-data allocator; internal stacks
and timing-critical state stay internal. Shared external BSS is explicitly
zeroed before service initialization.

Hardware checks exercised live/CSV plotting with responsive USB, repeated Plot
cleanup, catalog refresh, Hello Python installation/run/alias, and rejection of
unsupported Lua/Python graphics entries. TLS initially correctly rejected an
incorrect RTC date. Enabling mbedTLS assembly and NIST arithmetic optimizations
reduced certificate processing enough for the catalog GET to complete in about
four seconds. Diagnostic output is disabled by default (`SK_HTTP_DIAGNOSTICS=1`
opts in). The official catalog and Hello Python are retained for manual use.

Current firmware: `.pio/build/teensy41_display/firmware.hex`, SHA256
`bfba482edb0564644527967a4ed5fde7affaed799c61ca3ebcb90309defea755`.
Flash 1,015,088 bytes; RAM1 424,256 bytes; RAM2 225,704 bytes.
Firmware and validation logs: `../solar_os-baselines/2026-09-27-plot-playground/`.
Final combined app test: `/tmp/teensy-plot-playground-final.json`;
independent-console regression: `/tmp/teensy-plot-display-regression.json`.
Both pass. HTTP/settings/child lifecycle/terminal host tests pass, as do the
14 manual tests and the USB-only synth build. The user confirmed a readable live Plot graph and working Space pause/resume
and Q-to-shell keyboard controls.

## Continuing work

The display now also supports native Plot graphics and the Playground catalog
browser; see [Plot/Playground notes](teensy41-plot-playground.md). Lua and Python
graphics bindings remain unavailable. Other graphical apps still need porting. Singleton apps and the single worker/audio resource
remain shared; native blocking operations without cooperative polling can delay
both consoles. Startup runs on the local LCD session once per boot; USB geometry
remains persistent while LCD geometry is fixed. Ethernet requires `network up`
after reboot; `network down` / `network up` restarts DHCP.

Next work should follow the user's chosen priority. Possible follow-ups are
combined display/audio validation after refitting the shield, home-built
keyboard compatibility, broader ANSI/Unicode or additional graphics primitives, and the
[roadmap](teensy41-roadmap.md). Do not automatically resume DNP3. Its preserved
scope was a two-way 3.3 V UART bridge on Serial7/Serial8, configurable from
9600 baud, then a TCP proxy/viewer; its sources remain unbuilt and untested.

Use separate build directories when needed and keep known-good firmware before
hardware changes. Serial tests must run one at a time. Host USB devices may be
hidden by the sandbox; use approved host access before reporting disconnection.
The loader sometimes retries a USB write and succeeds. A prior image above
1 MiB hit a loader/HEX boundary issue; the board's flash capacity is larger.

Final Files regression passed: SD/flash copy/move, recursive copy, ZIP, editor
and Python child return, and repeated app cleanup. The memory assertion accepts
increased internal free space while still rejecting losses; PSRAM must match.
Log: `/tmp/teensy-plot-files-regression-final.json`.
