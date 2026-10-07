# Concurrent application testing — 2026-10-06

Initially tested the installed recorder-era firmware after checkpoint commit
`7dd4e609`; the later stack-placement candidate is described below.
USB was `/dev/ttyACM0`,
Ethernet `192.168.1.197`, host fixture `192.168.1.100`. The user power-cycled out
of HalfKay before testing. Existing music in `/sd/music` was read only.

## Initial firmware result

**Confirmed:** concurrent SSH/FTP activity can wedge the board, including USB
recovery. It reproduced with no audio or Python. Reconnecting SSH during paced
FTP also wedged it, so upload throttling is not a reliable workaround.

**Passed:** SSH alone (50 requests), MP3 + Python + FTP without SSH (two minutes,
71 samples, seven verified transfers, zero observed audio underruns and exact
memory recovery), and one paced SSH/FTP run (55 requests, five transfers).

**Additional failure:** small-file FTP churn returned `425 Data connection
failed` while SSH and USB remained responsive. Its cause is not yet isolated.

DTCM connection-buffer pressure is the leading hypothesis, not a confirmed root
cause. No firmware fix, rebuild, or upload was performed. The last run wedged the
board again and the user confirmed buzzing. After the user power-cycled it, a
final read-only probe confirmed idle consoles, stopped jobs/audio, mounted SD
with zero open handles, and working Ethernet. The test process and its host
servers have exited. Evidence: `/home/dennis/teensy-multitask-final-status.json`.

## Console stack placement candidate

The main console used `xTaskCreate`, which allocated its 40,960-byte stack from
the small newlib/DTCM heap. Other console stacks already use OCRAM. The candidate
uses `xTaskCreateStatic` with a linker-reserved `DMAMEM` stack in cached OCRAM,
preserving the same stack capacity and priority. Its control block is static.
This frees DTCM for the networking library without changing application limits.
It does not make allocation failure safe for arbitrary workloads.

Built and flashed `teensy41_telnet_legacy`, preserving the existing pin mapping.
Candidate HEX SHA-256:
`2095869268b9ea872066782fe5db7fa7c32b8ffc0dc52d6f2923bab6e35d4994`.
The original ELF/HEX/map are saved in
`../solar_os-baselines/2026-10-06-multitask-before/`.

Linked verification places the 40,960-byte stack at `0x2020e8c0`, before the
OCRAM allocator pool at `0x20236c20`. Both
`test_teensy41_console_stack_host.py` and `test_teensy41_usb_driver_host.py`
pass against the candidate ELF; USB DMA buffers remain in DTCM. Idle DTCM rises
from 31,492 to 72,464 bytes (72,400 after warming network services).

The unpaced SSH/FTP reproduction passes 120 seconds, 74 SSH requests and eight
verified file round trips, followed by successful cleanup. Minimum sampled
DTCM is 47,140 bytes versus 6,168 in the earlier failing run. Final memory is
368,332 internal / 8,123,292 PSRAM bytes free. Evidence:
`/home/dennis/teensy-multitask-stack-fix-network.json` (161.845 seconds total).
This supports the memory-pressure hypothesis; no fault trace has established
the exact original failure mechanism.

The full unpaced test also passes (468.886 seconds total):

- Three minutes of MP3 + Python + FTP + SSH, 63 samples, advancing Python rows,
  and a music track transition; zero observed playback underruns.
- Four retained Calc switches and SSH graceful/abrupt disconnect cycles while
  music and FTP continue, including the previously failing Telnet reconnect.
- Python reattachment to Telnet and survival after detaching/disconnecting.
- Four combined startup/stop cycles, each returning exactly to the warmed
  service baseline of 321,148 internal / 8,103,480 PSRAM bytes free.
- Recording alongside Python/FTP/SSH without capture overruns. The downloaded
  WAV is finalized, mono 44,100 Hz, 16-bit, 1,587,200 frames.
- Twelve verified FTP round trips across the concurrent phases. Minimum
  sampled DTCM is 45,976 bytes. Cleanup leaves zero SD handles and final free
  memory of 368,100 internal / 8,123,292 PSRAM bytes.

Evidence: `/home/dennis/teensy-multitask-stack-fix-all.json`. No power cycle was
needed between the candidate network-only and full runs. The regression checks
now support keeping these app combinations enabled; this finite test is not a
guarantee against memory exhaustion with other workloads.

A final unpaced small-file SSH/FTP repeat also passes: 90 seconds, 56 SSH
requests, and eight completed verified file round trips including cleanup's
in-flight transfer, using 4,100/1,028-byte files. Evidence:
`/home/dennis/teensy-multitask-stack-fix-small.json`. The earlier FTP 425 error
was not reproduced, but its precise cause remains unconfirmed. The board was
left with test services stopped, audio off and zero open SD handles.

## Observed failure: concurrent SSH/FTP/MP3/Python hang

A run launched these workloads together:

- LCD-owned player, retained with Ctrl+Z, playing `/sd/music` with repeat all.
- Detached MicroPython: 10,000 integer operations, one flushed SD log row,
  and a 50 ms sleep per iteration.
- Host FTP client uploading/downloading binary files to the Teensy FTP server.
- Telnet-owned SSH client connected to an isolated host fixture, requesting
  4 KiB output and short echoes.

In the second combined run, the first 4 KiB SSH response stopped partway through.
The Telnet reader timed out after 15 seconds; the FTP client also timed out.
USB subsequently returned no output for `close 10`, `close 11`, `telnetd stop`,
`job stop ftpd`, or `sd status` (30 seconds allowed per command). USB still
enumerated as Teensyduino Serial. No firmware assertion or fault text was captured.
The SSH fixture reported no server-side exception.

The last successful audio status, before the stall, showed 3,356 blocks,
zero underruns, and active playback. The user subsequently confirmed that the
speaker was only buzzing: normal playback had failed along with USB and network
responsiveness. A stuck/repeated audio buffer is a possibility, not an established
cause. The exact failure mechanism remains unresolved.

Bench constraint: the USB host keyboard cannot be connected at the same time as
the audio shield. Physical LCD keyboard responsiveness therefore could not be
checked. Further audio coexistence tests must use USB serial and Ethernet;
LCD-owned apps can still be launched through the existing `lcd send` commands.

Evidence: `/home/dennis/teensy-multitask-03.json`. Its elapsed time is 201.962 s,
including failed cleanup attempts. The failed SD fixture was preserved at
`/sd/_multitask_7774c6e9`. Do not assume its apps/services were cleaned up.

## Reproduction without Python

After another user power-cycle, `--mode no-python --soak-only --seconds 90
--payload-kib 64` reproduced the stall with only LCD MP3 playback, FTP traffic,
and Telnet-owned SSH. The last SSH status was `starting SSH handshake`, before
any soak samples. The user again confirmed buzzing from the speaker. The last
successful audio status had 672 blocks and zero underruns. USB `close 5` and
`telnetd stop` timed out. This establishes that Python is **not required** for the
failure; it does not yet distinguish audio/SSH interaction from FTP involvement.

Evidence: `/home/dennis/teensy-multitask-no-python.json`; preserved SD fixture
`/sd/_multitask_1168b7d1`. Idle FTP and SSH setup succeeded before playback.
The next isolation tests are `--mode network-only` (SSH/FTP, no audio or Python)
and `--mode audio-ssh` (MP3/SSH, FTP stopped before the tested handshake).

## Reproduction without audio or Python

After another power-cycle, `--mode network-only --soak-only --seconds 90
--payload-kib 64` also stalled. SSH and FTP were active through the Telnet-owned
SSH session; no player, recorder, or Python worker was started. Eleven SSH
requests succeeded and a 65,540-byte FTP round trip was verified, including
listing/rename/delete, before a subsequent SSH request timed out with no output.
USB `telnetd stop` timed out too. Thus **neither audio nor Python is required**.

Evidence: `/home/dennis/teensy-multitask-network-only.json`; preserved fixture
`/sd/_multitask_55debb33`. Last sampled memory: DTCM 6,168 bytes free, OCRAM
283,572 bytes free, PSRAM 8,006,448 bytes free. This suggests investigating
connection-buffer pressure on the small DTCM heap, but does not establish OOM.
QNEthernet's `ConnectionState` reserves a `std::vector` receive buffer using the
C++/newlib heap; those buffers do not use SolarOS's larger OCRAM/PSRAM pools.
USB recovery failed and the test ended after 95.237 seconds including cleanup.
The next isolation case is `--mode ssh-only` (FTP stopped before the tested SSH
session, no audio/Python), followed by further FTP connection churn checks.

## SSH-only, small files, and paced uploads

- **SSH alone passed:** 50 echo/bulk requests over 60 seconds, with FTP stopped
  during the tested session and no audio/Python. DTCM stayed near 19,268 bytes
  free. The complete setup/run/cleanup took 89.602 s. Evidence:
  `/home/dennis/teensy-multitask-ssh-only.json`. Services stopped successfully;
  the then-current harness left its successful fixture `/sd/_multitask_67aa950c`
  because FTP had already been stopped. That harness cleanup condition is fixed.
- **Small FTP transfers stayed responsive but returned a protocol error:** with
  SSH active, 37 requests and five verified 1,028/4,100-byte FTP round trips
  completed. An additional transfer ended with `425 Data connection failed`.
  USB cleanup succeeded, SD handles returned to zero, and memory recovered to
  368,316 internal / 8,123,292 PSRAM bytes. This is a separate observed failure,
  not a hang or a passing run. Evidence:
  `/home/dennis/teensy-multitask-network-small.json`; failed fixture
  `/sd/_multitask_f909dfa1` retained.
- **Paced larger uploads passed:** sending 512-byte chunks with a 120 ms delay
  between chunks allowed SSH plus 16,388/65,540-byte FTP transfers to complete:
  55 SSH requests over 90 seconds and five verified round trips including the
  in-flight transfer completed during cleanup. Zero cleanup errors; zero SD
  handles; memory recovered to 368,316 internal / 8,123,292 PSRAM bytes.
  Evidence: `/home/dennis/teensy-multitask-network-paced.json` (131.716 s total).
  Its temporary SD fixture was removed.

The size/rate dependence strengthens the receive-buffer pressure hypothesis,
but is not proof of a particular allocation failure. Besides reserving a TCP
window per connection, QNEthernet copies unread bytes to a second vector when
EOF arrives, before releasing the original buffer (`maybeCopyRemaining`). Its
vectors use DTCM/newlib rather than SolarOS's larger memory pools. No allocation
failure or fault PC was captured, so a fix is not claimed. The `425` condition
also needs an isolated reproduction; it must not be conflated with the hang.

## MP3 + Python + FTP without SSH passed

`--mode no-ssh --seconds 120 --payload-kib 64 --soak-only` completed 71 samples
and seven verified FTP round trips (including the in-flight transfer at cleanup).
The retained LCD player reported zero underruns, its output advanced throughout,
and the detached Python CPU/SD logger advanced from row 0 to row 105. The Telnet
shell remained responsive, with sampled echoes around 0.24–0.32 seconds.
FTP uploads were unpaced in this case. All owned sessions/services cleaned up;
SD handles returned to zero and memory recovered to 368,316 internal / 8,123,292
PSRAM bytes. Total duration, including setup/cleanup: 159.578 s.
Evidence: `/home/dennis/teensy-multitask-no-ssh.json`. Temporary SD fixture removed.

## Paced combined workload: reconnect still hangs

The final full run used 512-byte FTP chunks with a 120 ms upload delay while MP3,
Python, and SSH were active. Its initial 60-second soak passed 22 samples and two
completed verified FTP round trips with zero observed audio underruns. USB Calc
computed 123 + 456 = 579 and was retained. A Telnet disconnect closed its SSH
client; playback continued, and a new Telnet shell answered an echo.

Starting SSH again from that reconnected shell stalled at `starting SSH
handshake`. USB `close 17` then timed out. The third FTP transfer had downloaded
its file, but did not finish the complete verification/metadata sequence. This
run is **failed**, and paced uploads are **not** a demonstrated workaround.

Evidence: `/home/dennis/teensy-multitask-lifecycle.json` (181.217 s total);
preserved fixture `/sd/_multitask_b2e2f934`. Cleanup stopped after the first USB
timeout to preserve the state and avoid repeated blocked commands. Later Python
reattachment, repeated heap-recovery cycles, and recorder/WAV checks were not
reached. No pass is claimed for those cases.

## Earlier combined run

The preceding run captured 58 successful audio/Python/SSH samples over roughly
162 seconds of concurrent operation, with zero observed audio underruns and
advancing Python log rows. A 1,048,580-byte upload/download round trip completed
byte-exactly in 186.039 s (including FTP connection and metadata operations).
SSH request latency was 1.326 s median / 2.339 s maximum across short and 4 KiB
responses. These are end-to-end times through both Telnet and SSH.

That test stopped because its assertion incorrectly assumed the audio block
counter could not reset between tracks. Playback remained active, and cleanup
succeeded: zero SD handles and all playback buffers released. The harness now
allows counter resets between songs. This assertion failure is not a firmware
failure. Evidence: `/home/dennis/teensy-multitask-02.json`; preserved fixture
`/sd/_multitask_54b6d4ad`.

Before the later hang, an idle 65,536-byte FTP upload took 3.528 s and download
2.016 s; an idle SSH echo took 1.007 s. The different transfer sizes and metadata
operations prevent a strict throughput comparison. Slower loaded transfers are
an observation, not independently established as a bug.

The initial SSH fixture attempt was rejected due to an existing saved host key
for the host's canonical address. No existing trust entry was removed. The test
uses an alternate textual spelling of the same numeric IP to isolate fixture
trust and establishes it before playback, avoiding flash writes during audio.
That setup failure is not a firmware failure. Evidence:
`/home/dennis/teensy-multitask-01.json`; preserved fixture `/sd/_multitask_40085238`.

## Repeatable harness and next checks

`scripts/ports/test_teensy41_multitask.py` requires `pyserial` and `paramiko`,
exclusive USB, idle consoles, mounted SD/music, and host LAN access. It starts
password-protected temporary FTP/Telnet services and a host SSH fixture which
never executes host shell commands. Success removes its unique SD fixture;
failure preserves it. One SSH known-host entry is added per successful fixture
setup. Logs include disposable test credentials.

```sh
python scripts/ports/test_teensy41_multitask.py \
  --seconds 90 --payload-kib 64 --mode no-python --soak-only \
  --log /tmp/teensy-multitask-no-python.json
python scripts/ports/test_teensy41_multitask.py \
  --seconds 90 --payload-kib 64 --mode no-ssh --soak-only \
  --log /tmp/teensy-multitask-no-ssh.json
python scripts/ports/test_teensy41_multitask.py \
  --seconds 90 --payload-kib 64 --mode no-audio --soak-only \
  --log /tmp/teensy-multitask-no-audio.json
python scripts/ports/test_teensy41_multitask.py \
  --seconds 180 --cycles 4 --payload-kib 64 --record-seconds 20 \
  --log /tmp/teensy-multitask-all.json
```

The full harness also contains retained Calc switching, SSH graceful/abrupt
closure, detached Python reattachment/disconnect, exact warmed heap recovery,
and concurrent recording/WAV validation. These were blocked by hangs on the
initial firmware and now pass on the console stack placement candidate above.
