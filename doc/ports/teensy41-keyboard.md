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
