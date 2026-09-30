# Teensy physical reconnect tests

Updated 2026-09-28. Physical observations below used the earlier USB-storage
firmware and powered host hub. The currently installed Telnet legacy image
retains SD recovery and includes the scope demo with physical ADC disabled;
the scope ADC40/AmpEn0 candidate is not flashed. See the
[master test checklist](teensy41-test-checklist.md) for the current queue.
No open USB test handles remained at the end of the recorded session.

## Observed physical results — 2026-09-28

On the existing USB-storage firmware (before SD recovery/OBD upload), the
operator completed safe USB eject/removal/reinsertion and confirmed keyboard
input remained usable. `/usb` disappeared on removal and remounted automatically.
All 8192 fixture bytes matched before and after reconnection.

Read-only surprise removal also passed: the old handle returned ENODEV and both
consoles remained responsive. Reinsertion with the stale handle retained blocked
both automatic and manual mount. Closing it returned ENODEV but released the
reference; automatic mount recovered with zero handles, and all 8192 bytes
matched again. These are single-cycle checks on the same drive. Different media,
active-I/O removal, repeated cycles and SD remain pending. Modifier reconnect
results are recorded below.
Keyboard-only removal/reconnect also restored physical input: absent/connected
status observed and the operator confirmed input works. The LCD showed
`keyboard123` entered as a command (unknown-command response), rather than the
requested mixed-case echo marker. USB stayed mounted and its fixture directory
was readable during keyboard absence. Held-Shift disconnect/reconnect passed: the operator confirmed lowercase
input, and the LCD dump shows `echo shift123` followed by `shift123`. USB remained
mounted with zero handles. Held-Ctrl disconnect/reconnect also passed: the operator confirmed normal
input and the LCD shows `echo ctrl123` followed by `ctrl123`; USB remains mounted
with zero handles. Held-arrow reconnect restored input, but the operator found no auto-repeat:
holding Left moves only once. Source confirms the Teensy adapter attaches only
a press callback and has no typematic timer. This is a missing feature, not a
passing repeat-cancellation test. Add repeat with release/disconnect cancellation
and rerun held-key removal checks. Keyboard and USB still report connected/mounted.

Detailed observations: `/tmp/teensy-physical-disconnect-progress.jsonl`.

## Current implementation

| Device | Current behavior | Remaining work |
| --- | --- | --- |
| USB flash drive | `/usb` auto-mount, file operations, safe eject/remount and busy-handle refusal tested | Single-cycle physical removal/reinsert and stale handles passed; repeated cycles and different media pending |
| USB host keyboard | Basic and held-Shift/Ctrl reconnect passed on the old USB image; repeat is missing | Implement repeat, then hold/release/disconnect tests and broader compatibility |
| SD card | Recovery installed with Clock image; host tests and boot mount passed | Physical detection, stale-handle and insertion validation |
| Computer-facing USB serial | Independent console reconnect previously tested | Repeat with the final hotplug firmware; this does not validate keyboard reconnect |

## Physical test order

1. **Keyboard alone:** unplug/reconnect at an idle LCD shell. From the computer
   console, `lcd` should change from `keyboard=absent` to `connected`. Verify
   ordinary text, Enter, arrows, Backspace, Shift and Ctrl after reconnect.
2. **Keyboard key state:** unplug while holding a letter, arrow, Shift or Ctrl;
   release before reconnecting. Check for stuck modifiers, unwanted repeated
   input, missing first keystrokes and stale queued input. Repeat in Files and
   Invaders, preserving the active app rather than restarting the board.
3. **Shared hub:** keep the USB drive connected while reconnecting the keyboard;
   verify the drive remains readable. Then remove/reconnect the whole hub with
   no writes active and check both devices recover.
4. **USB storage:** test normal `usb eject` followed by unplug/reinsert. Then
   hold a test file open read-only, unplug, verify old-handle operations fail
   without freezing either console, and reinsert before closing that handle.
   Remount must wait for stale handles to close; reopening must read the intact
   fixture. Test a different drive too, to catch stale-volume cache reuse.
5. **SD storage on the installed recovery image:** boot without a card, insert,
   verify `/sd` and file access; safely unmount/remove/reinsert; repeat with an
   open read-only handle and with a different card. `/flash` and `/usb` must
   remain usable. Verify no old handle can access the replacement card.
6. **Repeated cycles:** at least 20 reconnect cycles per device. Record mount
   state, errors, responsiveness, internal heap and PSRAM after handles/apps
   close. Include a boot with the keyboard and drive already attached.

Use unique fixture folders and hash-checked data. Start with idle/read-only
removal; removal during an active write is a separate test for disposable test
media, since filesystem damage is possible. Do not count an injected key or a
software remount as a physical reconnection test.

## Remote preparation

Source inspection, build checks, test automation and SD recovery implementation
can proceed remotely. Actual connector/card-detect behavior, power stability and
physical keyboard state need a local operator. Do not reboot or inject LCD input
merely to wait for that operator.

See [SD recovery and guided automation](teensy41-sd-recovery.md).

Relevant code: `sd_storage.cpp` owns the new SD lifecycle; `peripherals.cpp`
owns the keyboard and input queue; `usb_storage.cpp` owns removable USB state.
The keyboard adapter currently has no explicit disconnect-time queue reset;
test stale-input behavior before changing it, since queued input also supports
diagnostic command injection.
