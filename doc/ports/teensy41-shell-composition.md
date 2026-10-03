# PSRAM allocation and shell composition — 2026-10-03

Implemented in `teensy41_display` and inherited profiles, including
`teensy41_telnet_legacy`, with `SK_SHELL_COMPOSE=1`. Host tests, the legacy
firmware build, and USB/LCD hardware acceptance pass. Installed on the legacy
wiring board on 2026-10-03; background-worker PSRAM stack execution, repeated
cleanup, disconnect survival and reboot persistence have been exercised.

## Allocation policy

Keep the existing single 8 MiB PSRAM chip and QSPI flash. PSRAM remains one shared
heap after static EXTMEM storage, rather than fixed application partitions.
Eligible allocations share available space in request order; ordinary allocations
must leave a conservative 128 KiB system reserve. Allocation failure is reported
to the caller. Fragmentation can cause failure before total free space runs out.
Accounting includes conservative allocator overhead and takes constant time;
it does not scan the whole external heap on each allocation.

The `external-system` class can use that reserve and never falls back to internal
RAM. It is currently used by settings snapshots and shell pipe buffers. Application
heaps, RAMFS and ordinary temporary buffers cannot use it. The existing 512 KiB
RAMFS admission check also remains in force. All PSRAM allocations in this port
must continue to use `solar_os_memory_*`; bypassing the wrapper would bypass the
reserve accounting. ESP's shared allocator recognizes the new class as external
required; the reserve is a Teensy policy.

The cooperative background-script worker's 4096-word (16 KiB) stack now occupies
static EXTMEM. Its TCB stays internal. Main/interrupt, Ethernet, audio, console,
SSH and Python worker stacks retain their existing placement. Settings snapshots
also move from internal `calloc` to PSRAM: approximately 5.3 KiB per open handle,
up to four handles. These are placement decisions, not automatic migration of
old objects or stack overflow into another memory region.

## Commands

```text
date; time
setterm timezone Manitoba && time
ntp && date; time
mkdir /flash/example && cd /flash/example
cat /flash/example.txt | grep error | head -n 5
ls /flash | wc -l
```

`;` runs the next command regardless of status; `&&` runs it only after status
zero. Pipelines bind more tightly than either separator. In the NTP example,
`time` runs even if NTP fails because it follows `;`. Quoted and escaped operators
remain literal. Separators work with or without adjacent spaces. A trailing `;`
is accepted. The existing line limit is 191 bytes, with up to eight commands and
20 arguments per command. Syntax and command capabilities are checked for the
entire line before execution; argument validation remains command-specific.
Ctrl+C, Escape or console disconnect stops the rest of a chain at polling points.

Commands with audited status for `&&`:

```text
echo date time rtc ntp setterm identity mem uptime status top df port
cd cat mkdir cp mv rm ls pwd version board grep head wc
```

Additional synchronous commands allowed with `;`, but not as `&&` predicates:

```text
commands apps network ramfs flash sd usb gpio i2c spi uart expansion
ping netscan jobs
```

Interactive apps, `watch`, `wait`, nested `sh`, lifecycle commands and aliases
are not accepted inside chains. Run them separately. Foreground `sh FILE` can
execute composed lines inside the file. The background script runner retains
its existing single-command-per-line allowlist; it does not yet accept composed
lines. Completion still uses the existing single-command parser.

## Bounded pipes

Pipelines are sequential, with no new task or stack per stage. Each active
composed line containing a pipe temporarily allocates two 8 KiB PSRAM buffers.
The global payload budget is 64 KiB (four simultaneous pipelines); allocator
overhead is additional and charged against the system reserve. Buffers are freed
after success, error or cancellation. Plain `;` and `&&` require no pipe buffers.

Every intermediate result must fit in 8192 bytes. Overflow is an explicit error;
the next consumer is not run, and `&&` sees failure. A large file piped to `head`
can therefore overflow before `head` runs. This is not a streaming Unix pipe.
File `cat` in a pipeline preserves bytes and final-newline state; ordinary
terminal `cat` retains its existing 4 KiB display limit.

Producers: `echo`, `cat`, `ls`, the filters, and argument-free snapshots from
`date time rtc pwd version board mem uptime status top df port commands`.
Consumers: `cat` without arguments, `grep LITERAL`, `head [-n COUNT]`, and
`wc [-l|-w|-c]`, and a final `less` (no filename).

`commands | less` opens captured text in the interactive pager. `less` must end
the whole command line; following `;`, `&&` commands or another pipe are rejected
before any producer runs. The launch path retains one 8 KiB pipe buffer until
app start or failure. The pager copies at most 8 KiB plus a terminator into PSRAM
and releases it on exit; no temporary files or new tasks are created. Normal
scrolling/search/quit controls apply, including empty input.

`grep` does case-sensitive literal substring matching, with status 1 for no
matching lines. An empty match result can still feed the next consumer. `head`
defaults to ten lines and accepts 0..8192. `wc` without an option prints newline,
whitespace-separated word, and byte counts. ANSI styling is suppressed while
capturing output. Failed producer output is displayed on the console and its
remaining consumers are skipped; there is no separate redirectable stderr yet.
Redirection, `||`, background `&`, substitutions, regex grep, and live streams
remain unsupported.

## Validation

`bash scripts/ports/test_teensy41_compose_host.sh` runs ASan/UBSan coverage for
quote-aware parsing, preflight rejection, conditional short circuiting, real
shell I/O capture, filter behavior, exact-capacity and overflowing pipes,
cancellation, allocation failure, repeated cleanup, actual filesystem statuses,
and the production allocator's reserve/accounting logic with a host pool adapter.
Clock/timezone, settings persistence and failed-save behavior, background jobs,
and existing shell tests also pass.

Device acceptance (`scripts/ports/test_teensy41_compose.py`) passed USB/LCD
chains, real NTP query success/denial through `&&`, cancellation, exact-size and
binary pipes, overflow, 20-cycle exact memory recovery, and operation with 7 MiB
allocated to temporary RAMFS mounts. Warm free memory was 24,340 bytes internal
and 8,142,240 bytes PSRAM before and after the cycles; under RAMFS pressure,
802,140 PSRAM bytes remained. The fixture files and mounts were removed.

The existing `test_teensy41_jobs.py --reboot` suite passed four-slot admission,
cooperative wait/stop, 24-cycle memory recovery, disconnect survival, scheduling,
retained Clock alarm and persisted schedules after reboot. Manitoba timezone
also survived reboot. A subsequent background script left 13,612 bytes of stack
headroom. A cold script launch reported a 29 ms tick against the 25 ms budget;
this worker remains cooperative and is not a realtime execution guarantee.

Installed HEX SHA256:
`a6284fa1f08fc6e34d3cdc997d6097da2731dcc34e381b6d3d81ad3fd5240a49`.
Build: RAM1 439,936 bytes; RAM2 324,464 bytes; flash 1,428,240 bytes. RAM2 decreased
by exactly 16,384 bytes from the previous installed image. Evidence logs:
`/tmp/teensy-compose-device.json`, `/tmp/teensy-compose-jobs.json`,
`/tmp/teensy-compose-build.log`, `/tmp/teensy-compose-upload.log`.

Telnet-specific pipeline stress and simultaneous audio-load testing were not
performed in this acceptance run. Do not substitute the newer wiring profile.
