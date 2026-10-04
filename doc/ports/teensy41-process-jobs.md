# Detachable MicroPython processes (stage 2b)

An example is [background_logger.py](../../examples/teensy41/background_logger.py),
which appends uptime readings to `/sd/logger.csv` every 250 ms.

The original background implementation runs four bounded shell-script jobs.
Stage 2b connects one MicroPython execution worker to the session lifecycle:

```text
python /sd/logger.py
# Ctrl+Z
bg
jobs
job output ID
tail -n 10 /sd/logger.csv
fg ID
```

Use the numeric ID printed by Ctrl+Z; the same ID survives suspension,
background execution and reattachment. `fg ID` attaches a detached process to
the calling console, including USB/LCD/Telnet. `fg` without an ID prefers the
calling console's last retained app, then the detached Python process.
Ordinary retained apps still resume on their owning console.

`job stop ID`, `job kill ID`, or `close ID` requests cooperative cancellation.
The VM closes files/sockets on its own worker before its context/stack is reaped.
A script that deliberately catches cancellation can remain `stopping`; tasks
are never forcibly deleted while holding native resources. A stopped worker is
reaped automatically. A naturally completed detached worker remains `done` for
output inspection: `fg ID` shows its output and reaps it, or `job stop ID` reaps
it. Reap that completed process before starting another Python VM.

## Execution and I/O

- One VM at a time, preserving the existing singleton interpreter contract.
- A dedicated 40 KiB internal OCRAM worker stack is admitted through the SolarOS
  task API, separate from the existing foreground worker. The VM heap remains
  512 KiB in PSRAM. Admission/allocation failures reject startup.
- A private shell context preserves the working directory and arguments. The
  worker keeps its VM, native stack, open files and partial input across pause.
- `input()` is supported with bounded 4096-byte lines. When detached, it waits
  for reattachment without reading the shell's keys. An idle REPL can detach,
  but needs reattachment for further input.
- Last 8191 bytes of output are retained; `jobs` reports dropped bytes. `fg`
  replays that bounded transcript, then shows live output. Script slots retain
  their separate 4095-byte buffers and `script0`–`script3` names.
- VM hooks and native polling provide cooperative pause/cancel points. Native
  open/read/write/seek/flush/close calls release the console gate and use the existing storage mutex;
  pause waits at their return boundary. Other file users can still wait for a
  slow card, and synchronous native operations cannot be forcibly interrupted.
- Explicitly detached workers survive console disconnect. Foreground or merely
  suspended workers are cancelled on owner disconnect and retain resources until
  cleanup completes. No process survives reboot.
- Detaching is restricted to standalone text Python sessions. Graphical Python
  cannot be suspended while owning the display; a detached script cannot acquire
  graphics. Other interactive apps (Calc, Plot, editor) remain paused sessions,
  not runnable background processes.

`tail [-n 1..10000] FILE` reads a snapshot of the last lines with bounded memory
and cancellation. Default is ten lines; `tail -f` is not implemented.

## Scope and validation

This establishes the process/session foundation for MicroPython file and network
work. Lua workers, typed stream publishing and CAN/DNP3/serial/DAQ bindings are
not implemented by this change. In particular, `obd_logger.py` also needs an
available CAN/OBD Python API; the worker alone does not supply one.

Acceptance scripts:

```sh
python3 scripts/ports/test_teensy41_process.py --log /tmp/teensy-process-device.json
python3 scripts/ports/test_teensy41_process_edges.py --log /tmp/teensy-process-edges.json
```

They require idle consoles and exclusive USB, create unique SD fixtures, and
exercise logging while Calc runs, tail snapshots, safe pause/bg/fg, cross-console
reattachment, input preservation, singleton admission, disconnect policy,
cooperative stop, bounded output, graphics rejection and repeated cleanup.
Final-image device acceptance passes for both suites, plus focused Python LCD
graphics and the existing Python network suite. Eight repeated CPU-loop stop
cycles recover exactly to the warmed baseline: 29,020 internal / 8,163,984 PSRAM
free. Existing session and shell-job host sanitizer suites also pass. See the
handover for logs, installed image checksum and recovery snapshot.

The final test left the board at its shell with no Python process alive. Test
fixtures remain in unique `/sd/_process_*`, `/sd/_process_edges_*` and
`/sd/_solaros_pynet_*` directories; Ethernet remains up after the network test.

## Power-button shutdown

The [On/Off coordinator](teensy41-shutdown.md) resumes suspended workers,
exposes `solaros.shutdown_requested()`, and gives scripts a two-second grace
period before one interrupt. Python finalization remains on its own worker;
its file-I/O return boundary also honors shutdown resumption. Refusing to exit
leaves power on after the 15-second deadline. Ordinary Ctrl+Z/fg/bg and job-stop
behavior outside shutdown remains unchanged.
