# Small-display background DMM

The module-board profile provides `dmm`, a background DC-voltage meter on the
160x128 ST7735. Its FreeRTOS worker owns sampling, NES controls and drawing;
the launching shell returns immediately. It continues during other shell apps,
Python interpreters and FTP jobs. This is a meter service/command rather than a
foreground app session, so closing or disconnecting the launching shell does
not stop it.

## Current hardware contract

Dennis confirmed amplifier enable moved to pin 1 and the probe is on pin 40.
`teensy41_module_board` now uses AmpEn1 with the Serial1 console disabled; it
retains the existing display/SPI/NES wiring. ADC40 is claimed by the DMM only
while live acquisition is active, rejecting another owner's GPIO/ADC use.

**Live voltage measurement is disabled by default.** Both the nominal 5 V and
50 V DC front-end dividers, input protection and conversion scales remain
unconfirmed. `dmm start` reports this and does not configure or read ADC40.
`dmm --demo` uses generated data and is prominently labeled DEMO. Selecting a
software range does not control or replace an external physical divider.

After electrical verification, define `SK_DMM_INPUT_VERIFIED=1` and the actual
`SK_DMM_5V_FULL_SCALE_MV` / `SK_DMM_50V_FULL_SCALE_MV` values in the board profile.
These constants mean probe voltage in mV when the ADC reaches full scale; their
unverified defaults are 5000 and 50000. Live display remains labeled UNCAL.
The implementation uses the shared Arduino 10-bit ADC configuration and averages
eight readings per displayed sample; decimal digits are not an accuracy claim.
Near full-scale input is marked OL. It cannot detect the true voltage beyond a
clamped ADC input. Negative voltage, AC RMS, current, resistance and continuity
are not supported by this input/implementation.

Python ADC conversions and DMM conversions share a mutex without changing global
ADC resolution. Builds enabling the existing DMA scope ADC backend reject live
DMM acquisition, since that backend reconfigures ADC controllers independently.

## Temporary real-input bench test

With the user-installed 10k upper / 3.3k lower divider feeding ADC40, run
`dmm stop` followed by `dmm --test`. This opt-in test bypasses only the production
range-verification gate; GPIO ownership, the AmpEn pin check, ADC serialization
and exclusion of the DMA scope remain enforced. The screen shows `TEST UNCAL
5V`. The mode is not persisted or enabled at boot.

For this test only, nominal ADC full scale is estimated as 13.3 V at the probe
(3.3 V ADC reference times the nominal divider ratio). Production 5 V/50 V range
constants and persistent calibration are untouched. The estimate is not a
measurement of resistor values, ADC reference or front-end accuracy. At 5 V on
the probe the ideal ADC input is about 1.24 V; at 3 V it is about 0.74 V. Restrict
this bench setup to 0-5 V DC. The software does not protect the input electrically.
Both NES Down and `dmm range 50` are blocked while testing.

`dmm status` reports `source=ADC_TEST`, raw ADC counts and nominal ADC-pin
millivolts in addition to the estimated probe reading and live history. Sampling,
hold/min/max, the ten-second trace and incremental display updates work normally.
Stop before switching between demo and real-test sources.

## Operation

```
dmm --demo --range 5
dmm status
dmm hold
dmm min
dmm max
dmm live
dmm reset
dmm range 50
dmm stop
```

| NES button | Action |
| --- | --- |
| A | Freeze/unfreeze the large readout |
| B | Cycle LIVE, MIN, MAX |
| Left | Toggle AUTO / selected input range for the trace |
| Right | Toggle MAX mode |
| Up / Down | Select nominal 5 V / 50 V range |
| Select | Reset min/max to the current live reading |
| Start | Stop and restore the previous small-display surface |

Hold leaves sampling, extrema and the trace running. Changing mode releases
hold. Changing range clears readings, extrema and history to avoid mixing units.
Each new start begins a fresh measurement session. `dmm status` includes sample,
timing-gap, ADC-lock and display-miss counters, permitting background checks
without disturbing the display.

The worker polls NES independently of the shell, targets one measurement every
50 ms, and refreshes the reading/trace twice per second (NES controls can refresh
immediately). These are cooperative scheduling targets, not guaranteed realtime deadlines or peak-transient capture. A
201-point ring stores timestamps and millivolts. The bottom trace covers the
preceding ten seconds and breaks lines across gaps over
150 ms. Readings over 500 ms old are marked STALE. No SD writes or network
traffic are required for normal DMM operation.

The trace starts in AUTO mode. NES Left toggles it to the selected input range:
0-5 V or 0-50 V (the temporary ADC40 test is restricted to 0-5 V). The footer
shows AUTO, 0-5V or 0-50V. Shell equivalents are `dmm trace auto`, `dmm trace range`
and `dmm trace toggle`. Trace-only scaling does not reset history, hold or
extrema, and its preference survives input-range changes within a session.
Values outside a fixed plot range are clamped only for drawing; original
readings/history remain available. The trace and footer update incrementally.

## Display and lifecycle

The small dashboard pauses while the DMM holds its graphics lease. In fallback
console mode, the shell still runs and buffers text while the DMM covers it;
the NES Start button or USB `dmm stop` returns to the console and repaints it.
With a main display attached, that display keeps its normal shell/apps. An
existing Game Boy or other small-screen owner causes DMM startup to fail rather
than overwrite its screen. NES input is debounced and does not consume keyboard
events. Physical NES wiring/timing uses the existing shared FlexIO/DMA reader.

Only the worker releases its screen and live GPIO claim, after all in-flight
sampling/drawing finishes. Stop waits for that cleanup; shutdown also verifies
cleanup before proceeding. The worker's 8 KiB stack and history live in OCRAM;
it does not allocate a full-screen framebuffer. Text glyphs are replaced only
when their characters change, using opaque foreground/background writes. Static
labels stay untouched; a 540-byte trace raster is compared with its previous
state and only changed horizontal pixel runs are sent. Full-screen clearing is
limited to acquiring a new display lease, not periodic refreshes.

## Validation

```
bash scripts/ports/test_teensy41_dmm_host.sh
python3 tests/ports/test_teensy41_lcd_flush.py
bash scripts/ports/test_teensy41_hardware_host.sh
bash scripts/ports/test_teensy41_python_hardware_host.sh
bash scripts/ports/test_teensy41_shutdown_host.sh
pio run -e teensy41_module_board
```

Host tests cover conversion, hold/extrema/overload, both ranges, NES pressed-edge
actions, a ten-second history including timestamp wrap, scheduling gaps, real
renderer bounds/labels, stale samples, ownership exclusion, console repaint,
AmpEn1/ADC40 pin claims, Python resource regressions and shutdown cleanup.

`scripts/ports/test_teensy41_dmm.py --log FILE` runs the hardware demo acceptance:
it creates and removes a unique SD fixture, checks controls, runs CPU-bound
Python while transferring a 256 KiB file through FTP, compares received bytes,
and confirms DMM sample counts advance. Add `--leave-running` to leave the demo
visible after success. It never applies a voltage or synthesizes NES inputs.
Live divider calibration, physical NES controls, and measurement accuracy still
need bench validation.

Hardware demo acceptance passed on 2026-10-18 with the revised module-board
firmware and incremental renderer. The 256 KiB payload was identical after
upload and download while CPU-bound Python ran. DMM samples advanced from 40
to 383 with zero ADC/display errors and no additional missed intervals. During
hold, samples advanced from 23 to 40 while the large value stayed fixed. Stop
restored the dashboard and repeat start/stop succeeded.

Renderer tests assert zero periodic full-screen clears, no writes for an
unchanged frame, one glyph write for a single changed digit, stable held digits
and static labels, erased shortened fields, and only changed trace pixels. The
2 Hz visible refresh leaves the 20 Hz acquisition target unchanged. The user
confirmed the resulting display behavior.

Temporary ADC40 test acceptance also passed: real sample counts continued during
hold, both startup and runtime 50 V requests were rejected, the GPIO40 claim
was released on stop, and ADC/display error counters stayed zero. The final
firmware was left running in `ADC_TEST` mode. No known voltage was generated
by the acceptance test, and these checks do not validate voltage accuracy.

Trace-scale acceptance passed with live ADC40: switching AUTO to fixed-range and
back preserved the held value while sample counts advanced. Host tests cover
NES Left, fixed 5 V and 50 V axes, preservation across range changes, plot-edge
clipping, the footer labels and incremental repaint without full-screen clears.
The final live-input test was left running in AUTO mode.
