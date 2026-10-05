# Teensy USB keyboard repeat

Implemented 2026-09-29 in `keyboard_input.h` and the USBHost adapter.

- First repeat after 400 ms, then every 33 ms (about 30 Hz).
- Letters, numbers, punctuation, space, Backspace, navigation and keypad repeat.
- The newest repeatable key wins; releasing it does not resume an older hold.
- Shift/Caps Lock affect subsequent repeats. Ctrl/Alt/GUI chords, Enter, Tab,
  Escape, function keys and lock keys are single-shot.
- Repeat events are generated on demand: a stalled application never accumulates
  a backlog. A key release can finish one ANSI sequence already being consumed.
- Disconnect clears physical input; app changes and fresh prompts cancel repeat
  until a new physical press. Diagnostic injection is independent.
- Boot reports reconcile physical state, including modifier-only changes and the
  first key after reconnect. The key map is US English.

`lcd` over USB serial reports the held repeat key (raw HID usage), timing in ms,
press/repeat counters and dropped initial events. `rate=33` is the interval in ms.

Validation: `bash scripts/ports/test_teensy41_keyboard_host.sh` passes with
ASan/UBSan, including timing/wrap, release, disconnect, modifier changes,
application boundaries, rollover, atomic queue overflow, ANSI/keypad mappings
and 10,000 lifecycle cycles. The child lifecycle regression also passes.
The dual-console device regression also passed. The user confirmed smooth
physical repeat and release after testing letters, Left, Backspace and Shift
changes on 2026-09-29. Diagnostics recorded 144 repeats with zero dropped events.
Remaining physical checks: Edit/Files, simultaneous holds, disconnect while
repeating, app transitions and additional keyboards (checklist KEY-2 through KEY-5).

Use `teensy41_telnet_legacy` for the current AmpEn40 wiring.

## Saved Num Lock preference — 2026-10-04

```
setterm numlock on
setterm numlock off
setterm numlock
```

`on` saves the preference and immediately enables Num Lock on a connected USB
host keyboard. It enables Num Lock again when a keyboard is attached, including
at boot and after unplug/replug or a hub reconnect. Each new connection waits for the library’s SET_IDLE initialization request to
finish before sending an LED report, even if the driver retained an already-on
bit from the previous keyboard. The requests must not overlap because the USB
HID parser reuses its setup packet buffer. Caps Lock and Scroll Lock are preserved.

This is a connection default, not a forced lock: the physical Num Lock key
still toggles normally until the next connection. `off` disables automatic
enabling; it does not force the current lock off. Without a saved preference,
automatic enabling defaults to off. The installed bench preference is enabled.

The existing transactional NVS-compatible store saves namespace `keyboard`,
key `numlock`, in `/flash/.solar-settings/keyboard.bin`. Flash must be writable;
failed saves leave the live preference unchanged. Boot loads it before starting
USB host enumeration. `setterm` shows the preference; `lcd` shows `numlock=`
(the driver's current state) and `auto=` (the connection preference). The driver
status does not independently verify a keyboard's physical LED.

Validation: keyboard/settings host sanitizer regressions and port manual tests
pass. On-device on/off, invalid values and reboot persistence pass; the connected
045e:0750 driver reports Num Lock on after boot. Internal/PSRAM free memory
returns to the previous baseline. Physical LED/keypad reconnect verification
remains separately tracked as KEY-NUM.

The initial physical test reported a non-working LED (the Num Lock key itself
worked) and a reconnect problem. The installed ordering fix waits for SET_IDLE
completion before issuing SET_REPORT. Host tests explicitly cover that boundary,
reconnect, disabling a pending request, and not overriding later manual toggles.
User confirmed the corrected image: LED, manual toggle, reconnect auto-enable,
letters and keypad digits all work on the bench keyboard. Other keyboards remain
untested.
