# SD recovery candidate — 2026-09-28

Implemented for `teensy41_display` (`SK_SD_RECOVERY=1`), built and host-tested.
**Installed with the clock image on 2026-09-28; physical recovery validation
remains pending.** SD mounted successfully before and after software reboot.
The operator is away over SSH; wait until they return to test connector/card behavior.

## Behavior

- `sd [status|mount|eject]` reports mount state, open handles, generation, mount
  attempts, losses and driver error code. Eject refuses open files/directories,
  synchronizes the volume and prevents automatic remount until removal or an
  explicit `sd mount`.
- The host polling loop checks mounted SD media every 250 ms. A failed probe or
  sector operation marks the volume unavailable; file operations return errors.
  DAT3/pin 46 uses the bundled SD library's pull-down insertion-detection scheme
  while offline. Retry interval is two seconds; startup makes three attempts.
- POSIX files and directory iterators retain references until close, including
  after removal. The controller and volume cannot be reinitialized while these
  stale handles exist. Closing a stale handle may return an error but releases
  its reference. Reopen files after remounting.
- A guarded FsVolume blocks sector operations after loss. All paths use the
  existing recursive storage mutex. Sector transfers use an aligned DTCM bounce
  buffer for callers on cached LCD/PSRAM stacks. FIFO transfer completion is
  consumed before CMD13 probing, so probing cannot discard a pending write's
  completion flags.
- Other firmware profiles retain the original SD adapter. USB and flash keep
  their own mount lifecycles. No formatting is performed.

Do not use `SD.mediaPresent()` in this adapter: its automatic restart bypasses
our stale-handle protection. Do not open files directly through `SD.sdfs`;
that object only initializes the raw card. File access uses `sk_sd_volume()`.

## Verification and limits

Host lifecycle tests pass with AddressSanitizer/UndefinedBehaviorSanitizer,
including 10,000 simulated replacements with zero to sixteen retained handles,
failed discovery and safe-eject latching. These test the shared lifecycle state,
not the electrical interface or full SdFat integration. Seven Python tests
cover the guided runner's protocol, escaped LCD commands and failure reporting.
Display and non-USB network firmware builds pass; 14 manual-generator tests pass.

Physical SD card detect, reinsertion and stale-handle errors remain pending.
Basic keyboard reconnect passed on the earlier USB image; repeat and newer-image
regression remain in the [master checklist](teensy41-test-checklist.md). The bundled FIFO driver contains inner data-transfer waits;
this change does not establish a time bound for removal in the middle of an
active sector transfer. Begin with idle/read-only removal, then separately
investigate active-I/O fault behavior using disposable media. A replacement
must be observed offline before the adapter can invalidate old handles; rapid
unobserved swaps are not an established supported case. Surprise removal during
writes cannot promise filesystem integrity. Throughput impact of per-sector
synchronization/probing also needs measurement.

## Guided runner

Start with both consoles at idle shells, close apps using the target medium,
and attach the powered host hub. The runner owns USB serial exclusively and
uses the LCD interpreter temporarily for the open-handle check. It leaves a
unique `_hotplug_*` fixture directory for inspection and compares all 8192
fixture bytes after remount. It never formats, uploads or reboots.

The recovery code is installed. When the operator is ready for physical tests:

```sh
python3 scripts/ports/test_teensy41_hotplug.py --device sd --log /tmp/sd-remount.json
python3 scripts/ports/test_teensy41_hotplug.py --device sd --physical --cycles 20 --log /tmp/sd-hotplug.json
python3 scripts/ports/test_teensy41_hotplug.py --device usb --physical --cycles 20 --log /tmp/usb-hotplug.json
python3 scripts/ports/test_teensy41_hotplug.py --device keyboard --physical --cycles 20 --log /tmp/keyboard-hotplug.json
```

`--physical` requires an interactive operator: each unplug/replug waits for
`done`. Keyboard validation requires typing a unique mixed-case token on the
physical keyboard; injected input is not counted as keyboard validation. Each
storage run includes one stale-handle removal after the requested safe cycles.
JSON records operator actions, commands, completed cycles, heap reports and
failures. Aborted runs fail. Choose `--port /dev/ttyACM0` if autodetection is
ambiguous. The log's physical flag distinguishes software remount checks.

Also perform the [manual checklist](teensy41-hotplug-tests.md): boot without SD,
different replacement media, app key-state behavior and whole-hub reconnect are
not covered by the guided runner. If a run loses serial or cleanup fails, inspect
both consoles and close the test interpreter/handles before retrying.

Host-only checks, requiring no attached board:

```sh
bash scripts/ports/test_teensy41_sd_recovery_host.sh
python3 tests/ports/test_teensy41_hotplug.py
```
