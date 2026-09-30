# Teensy / SuperKeyboard handover

Updated 2026-09-29. Branch: `teensy41`; GitHub: `dennis1a4/solar_os`.
This is the current state. Older snapshots are in the
[handover history](teensy41-handoff-history.md).

## Installed firmware and wiring

Installed **`teensy41_telnet_legacy`**, with **AmpEn on pin 40**, Serial1 retained
and physical scope ADC disabled. Keep this profile until the AmpEn0/ADC40 move.
The normal `teensy41_display` image requires that wiring change and disables
Serial1. Both scope and USB-PD demo apps are available on the installed image.

Installed HEX SHA256:
`7ea498a91947d6a267714ab4f9bebb5b938494d74af818427c98bae432896c4d`.
Flash 1,340,592 bytes; RAM1 436,160; RAM2 271,192.
Microsoft keyboard `045e:0750`, powered USB host hub, RA8875 LCD, native SD,
USB drive, QSPI flash and 8 MiB PSRAM. Audio shield was absent in recent tests.
See [quick-start and wiring](README.md).

## Completed work and evidence

- Keyboard repeat: 400 ms initial delay, 33 ms interval; release/disconnect and
  app-transition cancellation. User confirmed letter, Left, Backspace and Shift
  repeat/release. Diagnostics recorded 144 repeats with zero drops. Host
  ASan/UBSan and dual-console device regression pass. See [keyboard notes](teensy41-keyboard.md).
- Workstation commands: embedded Help/Man, Watch, version/board/status/pwd,
  task Top, Port, DF, Date/Time, ZIP/Unzip and Ethernet Curl. Session/Sessions
  **only list fixed consoles**. Host and device suites pass; remote man/watch/
  session and Telnet lifecycle checks pass. See [audit and backlog](teensy41-workstation.md).
- Telnet: authenticated incoming shell, one client; device lifecycle checks
  pass and the user confirmed a real connection. See [Telnet notes](teensy41-telnetd.md).
- Graphics, View, Python graphics, Invaders and MQTT Explorer have recorded
  device/user checks. SD recovery is implemented and host-tested; physical
  SD removal remains unverified. Clock host/remote checks pass. CAN/OBD, scope
  and USB-PD have software/demo coverage; their hardware backends or validation
  remain incomplete. Follow the [master test checklist](teensy41-test-checklist.md).

The last keyboard upload rebooted the board. Subsequent console tests passed;
USB/LCD were responsive and the keyboard connected. Earlier Ethernet-up status
predates that upload: query `network status` before assuming it is running.
No media eject or shutdown was performed. No RTC/timezone change was made during
keyboard/workstation testing; a prior restart read 2018. Use `date YYYY-MM-DD`
and `time HH:MM:SS` to set local time, or the documented `rtc` UTC interface.
Correct UTC is required for HTTPS. Saved Manitoba timezone is fixed UTC-5.

## Next work

1. Complete remaining physical keyboard checks: simultaneous holds, Edit/Files,
   app transitions and unplug while repeating. Basic repeat/release is confirmed;
   do not repeat implementation work or mark all KEY tests complete.
2. User-selected workstation follow-up: retained sessions with fg/close, then
   bounded shell jobs/composition and network diagnostics. Listing sessions is
   not retained-session support.
3. Make the first DF scan cooperative: the attached media took 51.36 seconds
   cold versus 0.06 seconds cached, delaying both consoles during the first scan.
4. Work through SD hot-removal, Clock visual/audio, scope wiring/ADC, STUSB4500
   and CAN hardware prerequisites in the checklist. Keep demo and hardware
   results distinct. Home-built keyboard compatibility remains unresolved.

## Recovery and repository state

Local firmware/source/log checkpoints are in `../solar_os-baselines/`:
`2026-09-29-keyboard/` is the installed build;
`2026-09-29-workstation/` is its predecessor. These artifacts are not committed.
The keyboard snapshot predates the final user confirmation; current docs record it.

This cleanup commits the integrated port source, tests and documentation to
`teensy41`. Unrelated unfinished `solar_os_dnp3_bridge.*` and `src/vendor/opendnp3/`
remain local, untracked and outside the tested port. Do not resume DNP3 implicitly.
Unique SD workstation fixtures remain for inspection; no user files were removed.

For regression commands, see the [test guide](../../scripts/ports/README.md).
Run hardware suites one at a time with exclusive USB and an idle local keyboard.
No reflash is needed for this documentation/commit cleanup.

Cleanup validation: ten host suites passed (keyboard, child lifecycle, Clock,
graphics, MQTT, OBD, USB-PD, scope, SD recovery and Telnet), plus 14 shared manual
tests, 13 port manual/hotplug tests, 29 package tests and the linked USB DMA buffer
placement check against the installed ELF. Documentation links and diff whitespace
checks passed. Restored the core power service in the package manifest; the new
USB-PD service remains scoped to its own package.
