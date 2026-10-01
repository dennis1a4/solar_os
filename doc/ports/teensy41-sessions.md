# Retained Teensy application sessions

Implemented and flashed 2026-09-30. Stage 1 of the approved workstation
sequence. Legacy AmpEn40 host sanitizer and USB/LCD/Telnet device checks pass.
User confirmed calculator retention and Plot suspend/resume/redraw on 2026-09-30.

## Commands and behavior

- `Ctrl+Z` suspends the foreground app chain and returns to its shell.
- `sessions` or `session list` lists the three fixed consoles and app chains.
- `fg` resumes the latest suspended app on the calling console.
- `fg ID`, `session fg ID`, or `session switch ID` resumes the named app on its
  owning console, not on the console issuing the command. The owner must be at
  an empty prompt, without an active app, Watch, script or log-follow operation.
- `close ID` or `session close ID` stops the entire named app chain and **discards
  unsaved state**. Resume and save edited files first if they should be kept.
- Fixed console IDs are 1–3; monotonically assigned app IDs start at 4. Four
  suspended chains per console, plus its foreground chain; each chain remains
  limited to four nested apps. App frames and app buffers use their existing
  allocation policy (frames require PSRAM).

Only apps advertising resumable state and a resume callback can suspend.
Stage 2b adds a separate MicroPython worker: standalone text Python sessions
can suspend, run with `bg`, and reattach with `fg`. See [process jobs](teensy41-process-jobs.md).
Other synchronous commands do not gain preemptive suspension; Ctrl+C remains
their cancellation mechanism. Suspended apps receive no foreground key
or tick events. Existing Clock scheduling continues through its own service.
App claims remain held, including the shared audio reservation: suspension does
not permit another console to start a conflicting singleton.

Explicitly detached Python jobs survive console disconnect; attached Python is
cooperatively cancelled and reaped after its worker exits.

USB serial disconnect closes that console's active and suspended app chains.
Telnet disconnect does the same for its remote console. Physical keyboard
unplug does not close the always-present LCD console's sessions. Cross-console
requests are bounded and execute in the owning task under the console gate.

Dynamic shell creation, arbitrary console migration and upstream `session
create/send/focus` are not implemented by this stage. Fixed USB/LCD/Telnet shells
remain available; `close` targets app sessions only.

## Validation

```sh
bash scripts/ports/test_teensy41_sessions_host.sh
bash scripts/ports/test_teensy41_children_host.sh
python3 -m unittest discover -s tests/ports -p test_teensy41_manual.py
python3 scripts/ports/test_teensy41_sessions.py --log /tmp/teensy-sessions-device.json
```

The device script needs an idle local keyboard and exclusive USB. It creates a
unique `/sd/_sessions_*` fixture, exercises editor save after resume and checks
memory, cross-console requests and USB disconnect isolation. It does not validate
physical key chords or visual repaint quality.

Device evidence: `/tmp/teensy-sessions-device-final.json` verifies calculator
input, editor contents/save, memory recovery (29,948 internal / 8,202,420 PSRAM),
cross-console ownership, busy-Watch rejection and USB disconnect isolation.
`/tmp/teensy-sessions-graphics.json` verifies Plot suspend/text/resume, frame
progress and cleanup. `/tmp/teensy-sessions-telnet.json` verifies remote input
retention/resume, active plus suspended app cleanup, ten reconnects, network
recovery and listener restarts. Test fixtures remain in uniquely named SD paths.
