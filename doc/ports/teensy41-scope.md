# Single-channel scope — 2026-09-28

Native graphical `scope` app implemented for Teensy. `scope --demo` uses an
internal 100 Hz sine/square/DC generator. `scope` uses timer-paced ADC1 capture
on Teensy digital pin 40 (A16). The candidate changes AmpEn from pin 40 to pin 0
and disables the optional Serial1 UART console, which otherwise owns pin 0/RX.
USB serial and the LCD shell remain enabled. Other firmware profiles retain
the old pin map through build defaults.

**Physical ADC candidate not flashed. Move AmpEn wiring before installing the
normal display image.** The installed Telnet legacy image includes the scope
demo but disables ADC capture and still drives AmpEn on pin 40. Do not connect the scope front end to
pin 40 while that older firmware is running. The new ADC backend is compiled
but has not been exercised electrically; no sample-rate/accuracy claims are
hardware-validated yet.

## Existing code and reuse

SolarOS has an oscilloscope widget used by player, recorder, function generator
and web radio; it has no registered standalone ADC oscilloscope. That widget
normalizes amplitude automatically and consumes signed audio samples, making
it unsuitable for labeled, fixed-range voltage measurement without substantial
changes. The new app reuses the shared graphics API and app lifecycle.

The ADC backend uses the already-bundled MIT-licensed Pedro Villanueva
[ADC library](https://github.com/pedvide/ADC) (installed version 9.1), plus PJRC's
DMAChannel API. It follows the library's timer-triggered acquisition approach;
no external oscilloscope UI or new downloaded dependency was needed. License
headers remain in the bundled library. Sampling hardware is ADC1, ADC_ETC
trigger 0, QuadTimer4 channel 0 and one dynamically allocated DMA channel.
The ADC library initializes both ADC modules: do not run another ADC consumer
concurrently until a shared ADC ownership service is added. Current port audio
uses I2S rather than the on-chip ADC.

## Controls

Launch from the LCD shell. USB-only graphical launch is rejected by the app
registry. It can be driven remotely through existing LCD key injection.

| Key | Action |
| --- | --- |
| Space | Run/hold |
| + / - | Faster/slower timebase |
| Up / Down | Trigger level |
| T | Rising/falling trigger |
| N | Auto/normal trigger |
| S | Arm one triggered snapshot, then hold |
| R | Cycle raw 0–3.3 V, 0–5 V, 0–50 V scaling |
| W | Demo sine/square/DC |
| Q / Escape | Exit and release acquisition resources |

Ten horizontal divisions and eight vertical divisions. Shows rate, time/div,
V/div, trigger level/direction, capture count, minimum/maximum, Vpp, mean,
total RMS (including DC) and approximate periodic-signal frequency. Clipping
flags samples near ADC endpoints. A dashed line marks trigger voltage; T marks
the trigger at 25% of the displayed window. Min/max drawing per pixel preserves
narrow sampled pulses when multiple samples map to the same screen column.

## Acquisition and limits

- Twelve-bit single-ended ADC; hardware averaging disabled. Requested rates
  1–100 kSa/s depend on timebase (100 us/div through 50 ms/div). Display reports
  the timer-derived rate returned by the ADC library, not a calibrated clock.
- 1,024 samples per one-shot DMA block in a 32-byte-aligned 2 KiB OCRAM buffer.
  DMA disables on completion. The app stops the timer, invalidates data cache,
  then copies the coherent block into PSRAM. It never draws a buffer that DMA
  is still overwriting. Capture objects/DMA channel are released on exit.
- Foreground polling has a finite capture timeout. DMA has no scope ISR. Between
  blocks there are acquisition/display gaps; rare events may be missed. Normal
  and single modes search within each snapshot, not continuously across gaps.
- Display updates at most 10 times/second, usually less for real capture and
  slow timebases. The model uses a quarter-window of pre-trigger samples and
  hysteresis before detecting rising/falling crossings.
- Frequency is a simple crossing estimate requiring at least three crossings.
  It is not serial decoding, duty-cycle measurement, alias detection or a
  frequency counter. 100 kSa/s offers about 10 samples/bit at 9,600 baud, less
  than one at 115,200 baud. Full-rate UART capture is outside this first version.
  Audio waveform detail likewise depends on samples per cycle and front-end
  filtering. No continuous recording or export is implemented.

## Front end and board configuration

User selected positive-only ranges and will add reverse-polarity protection.
Software assumes full-scale maps linearly to ADC 0–3.3 V. Range is manually
selected and must match the jumper; there is no jumper sensing or calibration.
No bipolar input mode is included. Audio must be conditioned into the ADC's
positive input range. Divider tolerance, ADC reference and protection/filtering
will affect indicated voltages. These are troubleshooting estimates.

[PJRC specifies 0–3.3 V for the ADC input](https://www.pjrc.com/store/teensy41.html).
Neither software range selection nor ADC clipping indication protects the pin
from 5 V/50 V inputs. The external front end must perform that function.

The display build sets `SK_SCOPE_ADC_PIN=40`, `SK_AMPLIFIER_SHUTDOWN_PIN=0`,
`SK_UART_CONSOLE=0`, and `SK_SCOPE=1`. Compile-time assertions reject the direct
AmpEn/scope and AmpEn/UART-RX conflicts. Pin remapping is not automatic, and
these checks are not a general board pin-ownership system.

## Verification

`bash scripts/ports/test_teensy41_scope_host.sh` runs the actual measurement
model and app callbacks under AddressSanitizer/UndefinedBehaviorSanitizer.
Covers range conversion, both trigger edges, all timebases, RMS/frequency,
clipping, invalid captures, demo isolation, run/hold, single-shot, normal waiting,
small-screen draw bounds and capture-error/exit cleanup with a mock backend.
It also emits `/tmp/teensy-scope-preview.svg` from actual app drawing calls;
a raster preview is `/tmp/teensy-scope-preview.png` (host rendering, not LCD).
These tests pass. They do not execute ADC/DMA registers.

Final `teensy41_display` build passes, as do 14 manual-generator tests.
Build log: `/tmp/teensy-scope-build.log`. Firmware flash is 1,279,612 bytes
(15.7%); RAM1 static allocation 435,232 bytes (83.0%); RAM2 227,904 bytes
(43.5%). Compared with the power-app candidate, flash grows 9,936 bytes and
RAM2 grows 2,048 bytes for the DMA buffer; RAM1 decreases 128 bytes with UART
console removal and the other changes. App capture arrays live in transient
PSRAM. Runtime ADC object/heap overhead has not been measured on hardware.

Hardware follow-up after rewiring:
check pin 0 AmpEn and no pin 40 output, launch/exit memory recovery, grounded ADC,
a known divider voltage, a low-voltage periodic waveform, measured sample rate,
trigger controls and coexistence with USB/SD/audio activity. Validate physical
LCD readability separately. Preserve the prior Clock checkpoint for old wiring.
