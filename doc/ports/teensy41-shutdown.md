# Teensy On/Off shutdown

The `teensy41_telnet_legacy` bench profile uses the Teensy's dedicated **On/Off
pad**, connected through a normally open momentary switch to GND. It consumes
no GPIO and preserves LCD CS37/reset9/WAIT15, AmpEn40 and Serial1. The built-in
Program button is not the On/Off button.

Tap and release On/Off to request cleanup. A hardware long hold remains the
emergency cutoff and can interrupt filesystem writes. After shutdown, connect
On/Off to GND for roughly half a second and release to wake. Verify these timings
on the board; the firmware preserves the existing SNVS timing fields.

## Commands

```
poweroff --check
poweroff status
poweroff
```

`--check` runs the same cleanup but leaves the CPU powered. It really stops jobs,
closes COM terminals and serial recordings, releases console hardware leases,
and, if configured, requests USB-PD 5 V. It does not restore those jobs afterward.
`poweroff` requests the actual SNVS power-off after successful cleanup. It must
run separately, not in `watch`, a command chain or a pipeline.

Close all active and suspended apps other than Python and COM first. The initial
implementation conservatively blocks other apps, including editors and players,
so it never silently discards unsaved app state. Synchronous cancellable commands
(such as MIDI recording) observe the shutdown request through their normal cancel
poll and finish their own cleanup. A command that does not yield delays shutdown.

## Python

A running or suspended VM receives up to two seconds to observe
`solaros.shutdown_requested()` and return. After that, its polling hook delivers
**one** `KeyboardInterrupt` per shutdown attempt. `finally` and `with` cleanup
run on the existing VM worker stack; repeated shutdown interrupts do not cut
that cleanup short. A suspended VM is allowed to run during shutdown.

```python
import solaros
import time

with open('/sd/capture.txt', 'w') as log:
    while not solaros.shutdown_requested():
        log.write('sample\n')
        time.sleep_ms(100)
```

A script may catch the interrupt or block in a native driver. If it has not
finished within 15 seconds, shutdown fails and leaves power on; the flag clears.
No worker is forcibly deleted and no execution state is saved for next boot.
A later shutdown request is a new attempt with a new grace period/interrupt.
A previously suspended worker remains resumed after refusal/timeout so its
cleanup can finish. Completed cleanup is not rolled back or jobs restarted.

## Cleanup and failure behavior

The interrupt only acknowledges/latches the button event. A separate task with
an 8 KiB internal OCRAM stack coordinates cleanup. Its console-lock acquisition
is bounded; a stuck VM cannot keep that coordinator waiting on the console gate
forever. Shell dispatch and scheduler triggers pause while shutdown is pending.
Console owners perform app cleanup; background scripts close their inputs;
Python owns its VM finalization. Serial logs drain their bounded PSRAM queues
and close their files before the storage barrier.

The storage barrier refuses outstanding file descriptors and write/sync/close
errors observed during the attempt, then syncs mounted SD/USB roots and the
LittleFS device. Files remain mounted for `--check` or a refused shutdown.
Storage/driver calls must still return: a driver stuck inside native I/O leaves
power on and may delay error reporting. The deadline is checked again before
power cutoff, even if a cleanup callback returns late.

An STUSB4500 that has never been configured this boot is skipped, which supports
the unwired bench. After `pd open` has attempted to configure a controller,
shutdown requires an active monitor and a fresh confirmed 5 V contract. A closed
monitor, failed request, disconnect, mismatch or timeout blocks cutoff. This is
software integration, not hardware-validated voltage control. It does not
program controller NVM or detect a high-voltage contract left across a CPU reset
before `pd open`; safe startup defaults require board-level design/testing.

The final step mutes the configured amplifier pin and requests SNVS TOP. This
turns off the Teensy CPU; it does **not** switch the board's separately powered
3.3 V/5 V regulators, USB host power or LCD supply. External regulator EN wiring
and guaranteed PD fallback on emergency cutoff remain hardware work. `reboot`
continues to use its existing reset path and does not invoke this coordinator.

## Tests

- `test_teensy41_shutdown_host.sh`: actual coordinator with simulated lock/app
  readiness, storage/serial/PD failures, late cleanup, repeated requests and
  tick wrap. Sanitizers enabled.
- `test_teensy41_serial_terminal_host.sh`: serial draining across simultaneous
  loggers, idempotent shutdown, ownership and existing overflow/error cases.
- `test_teensy41_shutdown.py --log /tmp/teensy-shutdown-device.json`: USB device
  acceptance using `--check`, unique SD fixtures, idle/blocked apps, cooperative
  Python, a suspended CPU loop with a three-second finally, interrupt refusal,
  background script/serial cleanup and memory recovery. Start with idle consoles.
- Physical button cutoff/wake, long hold, actual rail sequencing and USB-PD
  measurements are separate items in the master test checklist.

2026-10-04 software acceptance passes on the installed legacy bench image;
see the [handover](teensy41-handoff.md) for its checksum and memory measurements.
The first suspended-script run found a native file-I/O pause boundary that also
needed shutdown resumption; the final fixture verifies its three-second finally
writes/closes successfully. Physical results are recorded separately.
