# Teensy background jobs and scheduling

Stage 2b adds [detachable Python processes](teensy41-process-jobs.md):
`Ctrl+Z`, `bg`, numeric job IDs, `fg`, `job stop`, and `tail`. The original
shell-script runner described below remains available.

The display profile now exposes `jobs`, `job` and `schedule`. It reuses the
SolarOS job lifecycle and persistent scheduling service with four cooperative
script slots. This does not enable the upstream daemon collection.

Create `/sd/demo.sh` with the editor:

```text
echo Started
wait 5
echo Finished
```

Then run:

```text
job start script /sd/demo.sh
jobs
job output script0
job stop script0
schedule add demo every 1h run /sd/demo.sh
schedule show demo
schedule run demo
schedule disable demo
schedule remove demo
```

`script` chooses a free slot; `script0` through `script3` choose a specific slot
and restart it if occupied. Each job starts at `/` with its own current directory
and retains the last 4095 output bytes. `jobs` reports discarded bytes and the
last runner error. Jobs survive USB/Telnet disconnects, but not reboot. Completed
output remains until the slot is reused. Output is retrieved explicitly; shared
runtime timing warnings may still appear on USB.

Supported script commands: `echo wait pwd cd ls cat mkdir cp mv rm version
board status mem uptime top port date time`. One line runs per tick; `wait`
yields until its deadline. Interactive apps, nested scripts, watch, pipes and
redirection are rejected. Lines have a 192-byte input buffer limit (including
line terminator), with at most 16 parsed arguments. Filesystem operations are
synchronous: cancellation occurs between commands and a slow operation can delay
other consoles. Ordinary command diagnostics appear in the job log; a completed
script does not guarantee that every file operation succeeded.

Schedules support `in`, `every`, `at`, `daily`, and `weekly`; actions are `alarm`
or `run /path.sh`. Calendar entries use the configured timezone and require a
valid RTC. Entries persist in `/flash/.solar/schedule.bin`; job state/output does
not persist. A full four-job pool increments the schedule's skipped counter.
Clock countdowns use the same scheduler as transient `_clock` entries. There is
no power-off wake or hardware RTC alarm interrupt support.

## Validation and recovery

Host runner tests exercise the actual shared job lifecycle and Teensy runner:
four-slot admission, deferred file open, cooperative wait/stop, invalid input,
allocation failures, missing files, bounded output and 1000 cleanup cycles under
ASan/UBSan. Shared schedule tests cover persistence/reload, calendar and relative
alarms, and queued script acceptance/rejection. Existing Clock host tests pass.

```sh
bash scripts/ports/test_teensy41_jobs_host.sh
python3 scripts/ports/test_teensy41_jobs.py --log /tmp/teensy-jobs-device.json --reboot
```

The device test requires idle consoles and exclusive USB, creates unique SD
fixtures, and removes its own schedule entries. `--reboot` verifies persistence.
Device acceptance passed on the installed image, including four concurrent jobs,
stop-during-wait, USB/LCD responsiveness, isolated directories/output, rejection
paths, bounded logs, USB disconnect survival, 24 repeated jobs with exact heap
recovery (29,040 internal / 8,174,064 PSRAM free), schedule skip/manual/relative
triggers, retained Clock alarm/resume/cleanup and persistence across reboot.
USB reconnect retries accommodate re-enumeration. Fixtures remain on SD under
`/sd/_jt5a0d30b2`; test schedules were removed. The LCD returns to its shell.
See the handover for image checksum and recovery snapshot.

The first jobs image crossed an ITCM bank boundary and failed to boot normally
(blank LCD/no USB). Keeping compiler math helpers in cached flash restored
32 KiB of RAM1 headroom; the corrected image boots and passes acceptance.
A link-time assertion now enforces 72 KiB of startup headroom, including the
8 KiB core stack guard.
