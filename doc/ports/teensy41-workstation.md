# Teensy workstation command audit — updated 2026-09-30

Comparison: https://solar-os.eu/docs/manual/index.html (website identifies
SolarOS 4.15.4); this checkout's version.txt is 4.13.2. The website is not the
Teensy's feature manifest. Commands below are assessed against the actual
source allowlist, command registry and platform adapters, not just package names.

## Why so many commands were missing

The Teensy still selects SOLAR_OS_SHELL_CORE_ONLY. Its explicit command table
was retained from serial-shell bring-up. Some shared handlers, applications and
services already existed but were not connected to that table. Others depend on
ESP-IDF or the upstream session/job/resource runtimes. Turning CORE_ONLY off
would expose missing services and pull in incompatible implementations.

Keep shared application logic; put hardware and runtime differences below it.
A command is not integrated merely because its name appears in help.

## Workstation integration in this change

- `help`: shared tree browser for the embedded manual, including child-page return.
- `man TOPIC`, `man -k QUERY`, `man --list`: shared manual search and Less pager.
- `watch [-n seconds] COMMAND`: shared shell implementation; periodic shell
  events now delivered on USB/LCD/Telnet, with q, Escape, Ctrl+C and Ctrl+] exit.
- `version`, `board`, `status`, `pwd`, `port [list]`: platform facts, shell directory,
  byte-stream ownership, memory/task/mount counts and last foreground exit status.
- `top`: real FreeRTOS task snapshot, priority, state, stack headroom in bytes,
  cumulative CPU. `watch -n 2 top` supplies a live view; this is not upstream ltop's
  interval measurement.
- `df`: actual mounted SD/USB FAT and QSPI LittleFS allocation counts in KiB.
  The first FAT free-cluster scan took 51.36 seconds on the attached 32 GB SD
  and 16 GB USB setup; a cached repeat took 0.06 seconds. This first scan is
  synchronous and can delay both consoles; moving it into cooperative background
  work remains a usability follow-up.
- `date [YYYY-MM-DD]`, `time [HH:MM[:SS]]`: local wall-clock reads/writes, preserving
  the other component and storing UTC in the RTC. Rejects invalid calendar dates,
  nonexistent DST times and overflow past the 32-bit RTC range. For an invalid
  RTC, first initialize UTC using `rtc set UNIX_SECONDS`.
- `zip`, `unzip`: expose existing shared ZIP handlers/service. ZIP worker waiting
  yields the console gate so the other consoles can continue. These handlers do
  not yet provide reliable cancellation of every long archive operation.
- `curl [-L] [-o FILE] URL`: shared HTTP app using the existing Ethernet/HTTPS
  adapter. Correct RTC time remains required for certificate checks.
- `session [list]`, `sessions`: inspect fixed USB/LCD/Telnet consoles and retained
  app chains. Added 2026-09-30: Ctrl+Z, `fg [ID]`, `close ID`, owner-console
  requests and disconnect cleanup. Four suspended chains per console. Dynamic
  shell creation and migration remain unavailable; see [session notes](teensy41-sessions.md).

`hexedit` was already registered alongside Edit; this change adds its manual
page. The manual selection overrides narrower Teensy contracts and excludes
unavailable services instead of embedding the full ESP command promise.

## Existing useful workstation features

Shell: shared cursor-aware Tab completion (commands, paths, sessions/jobs, settings),
history, quoting, globbing for supported path commands,
`echo`, `wait`, `sh`, `clear`, `commands`, `apps`, `identity`, `setterm`, `mem`,
`uptime`, `reboot`, `exit`, plus the new commands above. Startup scripts and
identity/USB terminal preferences persist in flash. SolarOS scripts are their
own small command language, not POSIX sh.

Files: `cd`, `ls`, `cat`, `mkdir`, `cp`, `mv`, `rm`; Files, Edit/Hexedit, Less,
Notes and Sheet. Three storage mounts (/sd, /flash, /usb), archive support,
MicroPython file I/O and binary copy/move. Cross-volume directory moves, full
POSIX metadata and power-loss atomicity are not established.

Network/applications: Ethernet status/routes/up/down, SSH client, Telnet server,
MQTT Explorer, Playground, Python TCP/UDP and HTTPS downloads. Native Clock,
Plot, View, Invaders, audio player/recorder and Synth are integrated within the
limits recorded in their feature notes. Graphical apps use the local LCD.

## Remaining useful work, in dependency order

| Area | Commands/apps missing or incomplete | Required work |
| --- | --- | --- |
| Session control | Dynamic shell creation and broader `session create/send/focus` | Retained app chains, fg/close and owner-console requests are now integrated; creating additional shell sessions remains separate work |
| Background work | Broader runtimes and daemons | Four shell-script jobs and persistent scheduling pass acceptance. Stage 2b adds one detachable Python worker with Ctrl+Z/bg/fg, preserved I/O, safe stop and tail snapshots. Lua and broader daemon bindings remain unported. See [process jobs](teensy41-process-jobs.md). |
| Network diagnostics | More advanced scanning/time discipline | Stage 3 integrates `ping`, bounded TCP `netscan` and one-shot `ntp` with cancellation and checked responses. UDP scans, service fingerprinting and periodic clock discipline remain outside scope. See [diagnostics notes](teensy41-network-diagnostics.md). |
| Remote files and keys | FTP, `sshkey`, `xfer`, Telnet client | Integrate apps/protocol services, file completion, lifecycle and host interoperability; SSH key service exists but CLI is not exposed |
| Monitoring | `ltop`, richer `status`, `log`, `stream` | Task/runtime adapters, useful log sink and typed stream registry; top currently reports cumulative CPU |
| Temporary storage | `ramfs`, richer `disk`, mount tooling | Extend path router and file/directory handles to volatile PSRAM filesystems; retain hot-removal generation guards; disk must not imply ESP partition controls |
| Shell composition | Pipes, redirection, command chaining, environment/substitution; grep/find/head/wc/sort-style tools and tail follow mode | These are product additions where not provided by upstream, not command-table toggles; require per-command input/output streams, exit status and bounded processing |
| Runtime coverage | Lua, broader Python APIs, full terminal preferences | Bind useful shared APIs to Teensy services; existing MicroPython differs from desktop Python |
| Package loading | `pkg`, `load`, native modules | ARM/Thumb ABI, relocation, loader safety and toolchain work; ESP native binaries cannot simply run on Cortex-M7 |
| Additional apps | Reader/Writer, paint, launcher, other graphics/text apps, funcgen | Individually audit package dependencies, display/audio ownership and memory |
| Optional network services | `mqtt` daemon, WireGuard, agent, email, chat/inbox/contacts/gateway/outbox/messages, OSC/Link | Potentially useful, but service/task/persistence dependencies are substantially larger; not required for a workstation shell |

The approved sequence is retained app sessions, background jobs/scheduling,
network diagnostics, hardware resource management, then RAMFS. Retained app
support is implemented and user-confirmed; dynamic shell creation
is not part of that implementation. Follow the roadmap for stage status.

## Useful when the matching hardware/services are ready

Stage 4 installs `gpio`, `i2c`, `spi`, `uart`, `expansion`, inspection `io` and
shared resumable `com` with board reservations and fixed routing. See
[hardware coverage and limits](teensy41-hardware-resources.md). Physical UART8
loopback passed; external I2C/SPI peripheral interoperability remains untested.

`led`, `pwm`, `adc`, `onewire`, `input`, `control`, `display`, `midi`, `osc`,
`logic` and DAQ still need their respective hardware/service adapters.

`temperature`, `humidity`, `imu`, `gnss`, `nfc`, `haptic`, `neopixel`, `radio`,
`meshcore`, `pocsag`, `modem`, `charger`, `battery`, and advanced `power` depend
on fitted expansion hardware. CAN/OBD, STUSB4500 and scope work keep their
separate hardware prerequisites. `say` also needs its speech engine/assets.

`wifi`, `ble`, `espnow`, ESP `ota`/`nvs`, and ESP sleep/deepsleep cannot be copied
as Teensy hardware functionality. Wi-Fi/BLE would require external hardware;
settings/update/power equivalents need Teensy implementations. `engine` metrics
and D-pad/gesture support require relevant providers. An ESP programmer app
could theoretically run on Teensy UARTs, but it is separate from updating Teensy.

## Validation

See the handoff for installed-image status. Build profile is
`teensy41_telnet_legacy` for the current AmpEn40 wiring. Do not substitute normal
`teensy41_display` until the documented wiring move is complete.

Host checks:

```sh
python3 -m unittest discover -s tests -p test_generate_manual.py
python3 -m unittest discover -s tests/ports -p test_teensy41_manual.py
bash scripts/ports/test_teensy41_clock_host.sh
```

Device acceptance (idle keyboard; exclusive USB; creates unique SD fixtures;
starts Ethernet and a temporary local HTTP server):

```sh
python3 scripts/ports/test_teensy41_workstation.py --log /tmp/teensy-workstation-device.json
```

Physical LCD readability, keys and broader media/long-running behavior must not
be inferred from serial diagnostics. Existing pending hardware checklist items
remain pending.

### Recorded 2026-09-29 results

Final workstation device suite passed in `/tmp/teensy-workstation-device-final.json`.
Manual, watch, diagnostics, session listing, archive byte verification, HTTP text/
binary hash verification, cancellation and LCD/USB curl isolation passed. Five
manual cycles and five curl cycles returned exactly to 30,692 internal free bytes
and 8,202,420 free PSRAM bytes. Cancellation returned the prompt immediately and
recovered the same memory within the bounded check after the delayed peer closed.
The earlier suite assumed immediate transport-buffer reclamation; the final
check measures bounded TCP cleanup without resetting Ethernet.

14 shared manual tests, six Teensy manual-selection tests, Clock/date/time
sanitizers and child-lifecycle sanitizers pass. Clock setters have host coverage;
device tests read date/time without changing the user's RTC/timezone.

Workstation checkpoint build (superseded by keyboard repeat): flash 1,337,688 bytes, RAM1 435,616, RAM2 271,192.
RAM1 remains unchanged from the prior Telnet image. Normal display (new wiring)
and USB-only apps profiles also build; only legacy was uploaded.

Telnet regression passed in `/tmp/teensy-workstation-telnet.json`, including
remote man/watch/session plus authentication, window resizing, disconnect
cleanup, ten reconnects and immediate listener restarts. Dual-console regression
passed in `/tmp/teensy-workstation-display-regression.json`.

Recovery checkpoint: `../solar_os-baselines/2026-09-29-workstation/`.
