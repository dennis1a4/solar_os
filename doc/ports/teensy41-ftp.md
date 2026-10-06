# Teensy 4.1 FTP client and server

Added to `teensy41_display` and its `teensy41_telnet_legacy` derivative on
2026-10-05. **Installed and live-tested** on the unchanged legacy bench wiring.
The Teensy was found on `/dev/ttyACM1` after checking outside the filesystem/USB
sandbox. Ethernet address during acceptance: `192.168.1.197`.

## Client

Bring Ethernet up, then open the shared two-pane file manager:

```text
network up
ftp
ftp 192.168.1.10 2121
```

With no host, F2 opens connection setup, including username and password.
Anonymous login and port 21 are the defaults. Tab switches panes, Enter opens
an entry, F3 views, F5 copies, F6 moves, F7 creates a directory, and F8 deletes.
The footer shows available actions. Use `man ftp` for the embedded help.

`player /sd/music`, then Ctrl+Z, keeps playback running while using FTP.
Ctrl+] or F10 cancels an active FTP operation and exits the app. Other keys,
including Esc and Ctrl+C, are ignored while an operation is busy.

Optional arguments are `--user USER --password PASSWORD`, `--local PATH`, and
`--remote PATH`. Use the connection form to avoid entering a password in shell
history. The client supports binary transfers, EPSV with PASV fallback, directory
listing and file operations. Downloads stage beside the destination and replace
it only after a successful transfer. Remote upload behavior depends on the server.

## Server (host)

Export an existing directory explicitly:

```text
network up
mkdir /sd/share
job start ftpd /sd/share 2121 --user demo --password change-me
job status ftpd
jobs
job stop ftpd
```

On another machine, connect an FTP client to the Teensy's IP and port 2121,
using passive mode. The remote `/` maps to `/sd/share`. Paths normalize within
that export. Existing SD, flash, USB and RAMFS directories use the port's storage
layer; media must remain mounted for the transfer.

Port defaults to 21. Omitting both credential options enables anonymous access:
`job start ftpd /sd/share`. Authentication grants read/write access to the export.
FTP transmits credentials and files in cleartext; this implementation does not
provide FTPS or SFTP. It uses IPv4 and passive data connections (EPSV/PASV), with
one control client served at a time. Data ports are assigned dynamically. Active
PORT/EPRT transfers are unsupported. A passive data peer must match the control
client's IP address.

Uploads stage beside the target. A failed or stopped transfer removes its staging
file and preserves the previous target. Data I/O has a ten-second inactivity
timeout. `job stop ftpd` interrupts passive accepts and in-progress network I/O.
Stopping a job still waits for any current filesystem operation to return.
Network loss ends the server job; restart it after `network up` restores service.
There is no automatic startup. Shutdown stops FTP before storage synchronization.

## Port integration and memory

The shared `solar_os_ftp.c`, `solar_os_ftp_app.c` and `solar_os_ftpd_job.c` remain
the protocol/app implementation. `ftp_socket.cpp` supplies private socket
operations over the existing Ethernet RPC worker. It never calls lwIP from an
application task and never passes its descriptors to libc or SSH. The network
worker owns two FTP listeners, independently of Telnet, and rejects listener
port conflicts. Client DNS and socket waits honor cancellation.

The server uses an on-demand 16 KiB PSRAM stack, with internal task metadata.
A separate task reaps the stack after the worker suspends. It does not occupy
the Python background-worker slot or one of the four script slots. The client
uses the existing foreground worker allocation (12 KiB internal stack), so a
competing foreground worker can prevent it from starting. Listener metadata and
eight small FTP descriptor records are static. TCP connections share the existing
12-slot network pool. Payload RPCs are capped at 512 bytes.

Initial legacy-profile build: RAM1 432,832 bytes, RAM2 187,256 bytes, program flash
1,522,464 bytes. Compared with the preceding player image this adds 1,184 static
RAM1 bytes and 33,728 flash bytes. Live results are recorded below. The lower bring-up/network-only profiles are unchanged.
Python/Lua FTP bindings are not enabled by this port change.

## Validation

```sh
pio run -e teensy41_telnet_legacy
bash scripts/ports/test_teensy41_ftp_host.sh
bash scripts/ports/test_teensy41_jobs_host.sh
bash scripts/ports/test_teensy41_telnet_host.sh
python3 tests/ports/test_teensy41_manual.py
```

The FTP host suite uses AddressSanitizer and UndefinedBehaviorSanitizer:

- Actual port socket adapter: 100 binary transfer/cleanup cycles, partial chunks,
  EOF, timeout, cancellation, network failure, passive polling/accept, local/peer
  addresses and descriptor exhaustion.
- Actual shared client and server over loopback TCP: five binary round trips,
  authentication failure/success, listing, mkdir/rmdir/rename/delete, path traversal
  and command-injection rejection, failed-download preservation and cancellation.
- Stopping stalled uploads and passive accepts: completes within two seconds on
  the host, preserves the original target and removes staging files.
- Actual job command/registry with a mock FTP worker: start/status/list/stop and
  coexistence with the four script slots. Shared lifecycle regression includes
  1,000 script cleanup cycles.

Existing FTP feature checks, jobs/scheduler tests, Telnet tests and all 15 manual
checks pass. Firmware compilation uses the pinned QNEthernet library.

## Live acceptance — 2026-10-05

Initial installed HEX SHA256:
`3b38b1944194aabac1e625851e23cd5cdf2f8ff54776d8d1648c516e52834ebe`.
Artifacts and reports are in `../solar_os-baselines/2026-10-05-ftp/` beside the
repository. No firmware correction was needed during live acceptance; two test
runner readiness checks were corrected for asynchronous logs and the initial
`disconnected` screen.

- Server, tested with Python `ftplib`: password rejection, anonymous/password
  sessions, byte-exact binary transfers, empty files, SIZE, MLSD/LIST, PASV/EPSV,
  mkdir/CWD/rename/delete/rmdir, root confinement, Ethernet down/up and restart.
- A 65,798-byte SD upload took 4.505 seconds; download took 1.537 seconds. These
  are single observed transfers, including protocol overhead, not throughput
  guarantees. Stopping a stalled upload took 0.426 seconds and preserved the
  existing file, with no staging file left behind.
- Ten server start/transfer/stop cycles recovered the exact warmed idle heap:
  368,340 internal / 8,123,328 PSRAM bytes.
- Actual two-pane client, against host `pyftpdlib` 2.2.0: byte-exact 66,317-byte
  download and 33,547-byte upload, remote mkdir/delete, and ten connect/exit
  cycles with exact heap recovery. Teensy ftpd ran concurrently to verify the
  local file bytes. Cancelling a server that withheld its greeting returned to
  the shell in 0.022 seconds; PSRAM recovered exactly and internal free memory
  increased as TCP allocations were released.
- A 131,328-byte upload/download round trip passed while a Telnet shell and
  Python remained usable. Telnet echo took 0.299 seconds. Both directions of
  FTP/Telnet listener port collision were rejected. The server worker's minimum
  observed free stack was 10,368 bytes out of its 16 KiB allocation.
- `poweroff --check` stopped FTP before storage synchronization and left power
  on. FTP restarted successfully afterwards.

All unique test directories were removed, test services stopped, and SD reported
zero open handles. Final idle heap: 368,316 internal / 8,123,328 PSRAM bytes;
OCRAM fully free (336,888 bytes). Different test phases warm small shell/network
allocations, so compare repeated cycles against their own warmed baselines.

Scripts: `test_teensy41_ftp.py`, `test_teensy41_ftp_client.py` and
`test_teensy41_ftp_coexistence.py` under `scripts/ports/`. They require serial and
LAN access; the client test additionally requires `pyte` and `pyftpdlib`.
Physical media removal, USB/flash/RAMFS exports and FTP coexistence with audio
playback were not exercised in this acceptance run.

## Playback and connection-failure follow-up — 2026-10-05

During MP3/FTP coexistence testing the device halted in
`solar_os_task_delete_external`: its suspended-state assertion fired while
stopping the FTP server. A scheduler-state snapshot alone is not a completion
signal; a task blocked indefinitely can wake while its state is inspected.
FTP and shared foreground-worker reapers now require an explicit terminal marker
before checking suspension and deleting the task. Host regressions inject a
suspended snapshot for an unfinished task and verify it is not reaped/admitted.

Testing also reproduced audio underruns during large-directory scans and FTP
transfers. The bounded file decoder now runs at priority 2 alongside the consoles
and Ethernet worker, rather than the generic external-worker priority 1. Its PCM
queue provides backpressure; the internal feeder remains at priority 3.

Failed FTP handshakes previously used normal disconnect, sending QUIT and waiting
for another reply after the first timeout. They now close directly and provide
explicit timeout/invalid-response messages. The loopback regression asserts EOF
without a QUIT command from a client whose greeting timed out; it failed before
the fix and passes under ASan/UBSan afterwards.

The follow-up image uses RAM1 432,832, RAM2 187,256 and flash 1,523,204 bytes.
HEX SHA256:
`7ff95c2a51ca383efe4a67f73713f8e6bcad1559795ef3c1f12797bfbeaf5021`.

Live transfers with the player retained on the LCD passed byte-exact 66,317-byte
download and 33,547-byte upload, remote mkdir/delete, ten client reconnects with
exact warmed heap recovery, 45 ms cancellation, and five FTP server start/stop
cycles. The player reported zero underruns throughout. Final idle heap was
368,324 internal / 8,123,328 PSRAM bytes, with zero SD handles. Firmware, logs and
reports are in `../solar_os-baselines/2026-10-05-ftp-player/`.

On the same image, ten USB client launches from the large `/sd` directory passed
during background MP3 playback with zero underruns. Refused connections, early
server EOF, silent greeting timeout (10.615 seconds) and cancellation also passed.
The EOF fixture waits 250 ms after accepting so TCP establishment is observed
before closing; immediate closure can validly report connection refused instead.
TCP heap comparisons allow up to five seconds for asynchronous close, but still
require exact recovery. Final checks left both consoles at the shell, all jobs
stopped, audio buffers released, and zero SD handles.

Reproduce with an existing SD folder of MP3s and an anonymous FTP server:

```sh
python scripts/ports/test_teensy41_ftp_player.py --music /sd/music --local /sd --host 192.168.1.100 --log /tmp/ftp-player.json
python scripts/ports/test_teensy41_ftp_client.py --music /sd/music --log /tmp/ftp-player-transfers.json
```

The startup regression reads the supplied music/local folders without changing
their contents. The transfer test creates/removes its own fixtures and runs the
player on the LCD console while driving FTP over USB. Both require idle consoles.
