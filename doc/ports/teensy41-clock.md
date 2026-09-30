# Clock and timezone port — 2026-09-28

The existing shared graphical Clock app is enabled in `teensy41_display`.
**Flashed and remotely tested on 2026-09-28; visual/audio validation is pending.**
This image also includes the SD recovery and OBD demo changes. Earlier baseline
and candidate checkpoints are preserved; the board now runs the clock image.
Keyboard repeat remains an independent, recorded missing feature.

## Usage

```text
setterm timezone Manitoba
setterm timezone
clock
clock -s
clock -a 00:10
```

Clock must be launched at the LCD shell; the app registry rejects a USB-only
launch because it requires graphics. Stopwatch: Space starts/pauses, another
ordinary key resets. Escape/Ctrl-] exits. Countdown: exiting removes the alarm
and stops its sound. Clock displays four seven-segment digits; normal mode shows
HH:MM with a blinking colon, other modes MM:SS. No duplicate renderer was added.

`Manitoba` (also lowercase `manitoba`) means fixed UTC-5 throughout the year.
The internal POSIX form is `UTC5` (POSIX reverses the offset sign). It has no DST
rules and is deliberately not a historical IANA database. This matches the
user's requested permanent offset and the province's
[September 17 announcement](https://news.gov.mb.ca/news/?archive=&item=75397)
that clocks will not revert on November 1, 2026. UTC, fixed UTC offsets,
Europe/Berlin and explicit POSIX rules remain available. Shell completion
includes Manitoba. The initial default is UTC; no user setting is changed by
installing the firmware alone.

Timezones are persisted through the existing checksummed LittleFS/NVS adapter
in `/flash/.solar-settings/time.bin`, with upstream-compatible `tz_name` and
`tz_posix` keys. A failed commit leaves the active timezone unchanged. `rtc`
and TLS UTC remain unaffected; the clock applies local-time conversion only.
`rtc set UNIX_SECONDS` remains the explicit way to set the RTC. No automatic
NTP synchronization was added. An implausible RTC date yields dashed digits;
a plausible RTC date is not proof it has been synchronized or battery-backed.

## Implementation and boundaries

- Shared `src/apps/solar_os_clock.c` is compiled unchanged; app registry already
  provides the graphics requirement and allocated state lifecycle.
- `clock_services.c` adapts the UTC hardware read to the clock's datetime API,
  reuses shared timezone parsing, and supplies only the `_clock` transient
  relative-alarm subset of the scheduler API. It rejects persistent schedules,
  script actions, other names, duplicate alarms and out-of-range durations.
- Countdown uses the 64-bit monotonic timer, independent of RTC/timezone edits.
  The existing dual-console gate serializes service state and timezone changes.
  Console yields poll the alarm even when the clock is suspended, provided the
  console scheduler is running. No wake-from-sleep or power-off timer is claimed.
- Alarm requests a one-second 440 Hz tone every 1.6 seconds on an available
  SGTL5000 output. The separate alarm gate does not reset playback queues,
  change volume or commandeer the audio test tone. Existing queued/active PCM
  has priority. A missing shield is silent; 00:00 remains on screen. No physical
  sound validation has been performed, and long non-yielding operations can
  delay cooperative alarm delivery.
- App/timezone code is placed in flash. Existing sparse graphics presentation
  handles the colon and digit changes. Device frame counters advance correctly;
  visual inspection remains pending. Temporary display/expansion wiring was not changed.

## Host verification

```sh
bash scripts/ports/test_teensy41_clock_host.sh
pio run -e teensy41_display
```

The sanitized host test runs the actual shared Clock callbacks, the real port
clock adapter, shared timezone parser and real settings implementation, with
mock graphics/RTC/audio endpoints. It checks render geometry/hash changes,
invalid RTC, suspend/resume, stopwatch pause/reset and 32-bit tick wrap, countdown
rounding/delivery while suspended/repetition/cleanup, invalid durations, UTC and
fixed-offset signs, a seasonal POSIX transition, fixed Manitoba winter/summer,
failed-save rollback and timezone persistence across independent processes.
It does not test the audio ISR or electrical RTC/display behavior.

## Remote device verification

Uploaded successfully with `pio run -e teensy41_display -t upload`. Ran:

```sh
python3 scripts/ports/test_teensy41_clock.py --reboot --log /tmp/teensy-clock-device-retry.json
```

Passed USB graphical-launch rejection, LCD rendering activity, stopwatch
start/pause/reset, countdown completion and schedule cleanup, five repeated
launch/exit cycles, and timezone/RTC retention over software reboot. RTC was
set from host UTC and Manitoba saved. SD and USB mounted before and after reboot.
The first run encountered a transient serial reconnect error; the runner now
retries it, and the complete rerun passed. Frame counters do not verify pixels.
The audio shield reports missing; physical sound and LCD appearance remain
untested. Battery-backed retention across power loss is also untested.

Memory for this image:

| Resource | Measured usage or free space |
| --- | --- |
| Firmware flash | 1,264,660 / 8,126,464 bytes used (15.6%) |
| RAM1 build allocation | 435,360 / 524,288 bytes (83.0%) |
| RAM2 build allocation | 225,856 / 524,288 bytes (43.1%) |
| Internal heap, idle and Clock active | 30,964 / 79,360 bytes free |
| PSRAM, idle | 8,202,516 / 8,388,608 bytes free |
| PSRAM, Clock active | 7,790,220 / 8,388,608 bytes free |

Clock releases 412,296 bytes of PSRAM on exit. All five cycles returned exactly
to the same idle memory readings, also reproduced after reboot. Internal heap
is the tightest resource; these measurements cover Clock, not every possible
combination of suspended apps. Build allocations and runtime heap readings are
different measures and should not be added together.

Physical SD removal tests remain pending. Earlier USB/keyboard disconnect
checks used the older USB firmware. Keyboard repeat is still unimplemented.
