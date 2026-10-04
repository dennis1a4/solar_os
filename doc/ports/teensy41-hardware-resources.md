# Teensy hardware resources and serial terminals

Implemented and installed 2026-09-30: approved workstation stage 4.
The `teensy41_telnet_legacy` profile retains existing wiring. Shared SolarOS
resource claims protect board pins; the shared COM application runs through
Teensy UART adapters. No additional FreeRTOS task or dynamic UART buffer is used.

## Commands

| Command | Purpose |
| --- | --- |
| `io [pins|claims|buses|release]` | Inspect pins, ownership and fixed buses; release this console's raw claims |
| `gpio [list|PIN]` | Inspect header GPIO availability |
| `gpio mode PIN in|pullup|pulldown|out [0|1]` | Claim and configure a free pin (output defaults to zero) |
| `gpio read PIN`, `gpio write PIN 0|1`, `gpio release PIN` | Access/release this console's GPIO |
| `i2c [list]`, `i2c scan BUS` | Inspect buses or probe unreserved addresses; Ctrl+C cancels scan |
| `i2c xfer BUS ADDRESS HEX|- RX_COUNT` | Bounded write/read, up to 32 bytes each; contiguous hex or `-` for no write |
| `expansion [list]`, `expansion claim|release SLOT` | Claim a slot's SPI chip-select; UART ownership is independent |
| `spi [list]`, `spi xfer SLOT MODE HZ HEX` | Transfer through an already-claimed slot, mode 0–3, 100 kHz–12 MHz, up to 128 bytes |
| `uart [list]`, `uart open BUS [BAUD]`, `uart close BUS` | Claim/release a fixed UART; 300–1,000,000 baud, default 115200, 8N1 |
| `uart read BUS [COUNT]`, `uart write BUS TEXT` | Nonblocking raw reads (hex output) and text writes, up to 128 bytes |
| `com [--hex] [uart7|uart8|uart3]` | Shared interactive serial terminal; default uart7 |

The shell's 192-byte line limit can further limit payload sizes. UART writes
report bytes actually queued; partial writes return TIMEOUT and must be retried
by the caller as appropriate. Hardware subcommands, bus/slot names and GPIO
arguments use the shared completion provider API. `man` documents the commands.

## Fixed routing and ownership

| Bus | Pins / routing |
| --- | --- |
| i2c0 | SDA18, SCL19; address 0x0a reserved for the audio codec |
| i2c1 | SDA17, SCL16 |
| i2c2 | SDA25, SCL24 |
| spi0 | MOSI11, MISO12, SCK13 |
| spi1 | MOSI26, MISO39, SCK27 |
| slot0 | SPI1 CS37, I2C2, UART7 RX28/TX29 |
| slot1 | SPI1 CS36, I2C1, UART8 RX34/TX35 |
| slot2 | SPI0 CS9, I2C2, UART3 RX15/TX14 |

The current display reserves CS37, reset9 and WAIT15. Consequently slot0/slot2
SPI claims and UART3 opens are rejected; UART7 and UART8 remain usable. Existing
board controls, Serial1, I2S audio, bus signals, SDIO and QSPI are reserved.
GPIO commands expose header pins 0–41 only. Routing is not dynamically remapped.

UART acquisition atomically claims RX, TX and the UART resource. A conflict
leaves all three unchanged and does not initialize the driver. Raw USB/LCD/
Telnet claims have separate owners; GPIO roles cannot override UART/CS roles
even on the same console. Disconnect releases that console's raw claims.
GPIO and UART pins return to input; released SPI CS stays inactive high.
`io release` cannot release board reservations or COM's independent lease.

I2C commands temporarily claim an address and use existing bus mutexes and Wire
timeouts. Scans skip reserved addresses and yield/cancel between probes. SPI
uses the existing bus mutex and slot ownership check. Raw access is intended
for user-directed transfers, not automatic peripheral discovery/configuration.

## COM sessions

Close a raw UART before opening COM. To select a baud rate, open and close the
raw UART at that rate, then start COM on that bus. COM uses the last configured
baud, initially 115200:

```text
uart open uart8 9600
uart close uart8
com uart8
```

Ctrl+] exits and releases the UART. Ctrl+Z suspends the retained session while
keeping the UART lease; `fg ID` resumes and `close ID` discards it. Other consoles
cannot steal the lease. Disconnect cleans up the interactive app and its lease.

Each of three UARTs has an additional static 4 KiB receive ring in internal
OCRAM (12 KiB total), suitable for interrupt access. Reads/writes are bounded
and nonblocking. COM is an interactive session, not a detached logging worker.
Suspended sessions do not drain RX; buffers can overflow. Continuous high-rate
capture, lossless logging, hardware flow control, autobaud, alternative framing
and runtime language bindings are not supplied by this stage.

`io` is a one-shot inspection application, not the full upstream configuration
TUI. Expansion support is fixed routing and CS ownership, not peripheral driver
autoloading. PWM, ADC, one-wire and device-specific drivers remain separate work.

## Validation and recovery

- Host ASan/UBSan: actual resource/bus adapters, atomic claim rollback, protected
  pins, wrong-owner rejection, partial/blocked TX, repeated release and CS access.
- Seven embedded-manual tests pass.
- Final firmware device acceptance passed, including USB/LCD conflict rejection,
  COM suspend/fg/exit, raw and COM disconnect cleanup, completion and five cycles
  with exact recovery to 28,644 internal / 8,160,108 PSRAM bytes free.
- Physical UART8 RX34–TX35 jumper: raw write/read and COM terminal echo passed.
  UART8 was closed afterward; the user was told the jumper can be removed.
- External I2C/SPI peripheral transactions, sustained UART throughput and UART7
  physical loopback remain unverified. Protected I2C address rejection is tested;
  this is not evidence of peripheral interoperability.

Final HEX SHA256: `ab156c3f0e28c0000559e5e8673de2927f146279e0cf9aaff2038e9b0bc25576`.
Flash 1,407,624 bytes; RAM1 438,752; RAM2 340,848.
Evidence: `/tmp/teensy-hardware-device.json`; recovery checkpoint:
`../solar_os-baselines/2026-09-30-hardware/`.
Next approved stage is PSRAM-backed `ramfs` (original review item 7).

## Native serial terminal and background recording

The Teensy `com` app and `serial` command share a capture service for UART7,
UART8 and UART3. UART3 still conflicts with the LCD WAIT signal on the bench.
USB-host serial adapters are not integrated.

```
serial config uart8 9600 7E2 none
serial record uart8 9600 /sd/session.bin
com --enter crlf uart8
```

Ctrl+] exits COM; Ctrl+Z suspends it. Recording continues independently of COM,
the console connection and the suspended-session lifecycle. Finish explicitly:

```
serial status
serial stop uart8
```

`com [--hex] [--baud N] [--enter cr|lf|crlf] [BUS]` defaults to UART7,
configured baud (initially 115200), text display and CR. One terminal may attach
to each bus; other raw UART, MIDI and Python owners cannot steal it. Starting a
log on a terminal's bus is allowed when the baud matches. Stopping the log keeps
an attached terminal open. Exiting the terminal keeps a log open.

`serial config BUS BAUD FORMAT [none|xonxoff]` requires an idle port and changes
RAM-only settings for COM/serial; it does not change Python/MIDI/raw UART framing.
Supported formats are 8N1, 8N2, 7E1, 7E2, 7O1, 7O2, 8E1, 8E2, 8O1 and 8O2.
The number before parity is data bits; N/E/O means none/even/odd; the last number
is stop bits. Unsupported: 5/6/9 bits, 7N1, mark/space parity, 1.5 stop bits and
RTS/CTS. Baud is 300..1000000. `com --baud` or `serial record` supplies an explicit
baud without changing the configured framing/flow setting. An already active
port cannot be reconfigured.

XON/XOFF is opt-in. Received 0x13 pauses application TX and 0x11 resumes it;
while paused, terminal writes report an error rather than silently queueing
unbounded input. These control bytes are omitted from the live display but
remain in raw RX logs. The service sends XOFF at 75% queue occupancy and XON at
50%; it uses the log queue while recording, otherwise the display queue. A peer
must honor these controls. Leave flow set to `none` for arbitrary binary data.
Already queued UART TX bytes cannot be recalled by a received XOFF. Closing
retries a final XON for up to 100 scheduler ticks if the TX queue is full; failure
is reported as a timeout. Low-baud shutdown can briefly delay other UART polling.

`serial record BUS BAUD NEWFILE [--timestamp]` creates a new file exclusively.
Without the option it stores RX bytes exactly, including zero bytes and line
endings. With `--timestamp`, each text record contains elapsed milliseconds,
RX/TX and contiguous hex bytes, for example `123 RX 00ff0d0a`. TX records include
only bytes actually accepted by the UART driver, including generated flow
controls. Times mark chunks read by the capture task, not individual wire edges;
the millisecond counter wraps after approximately 49.7 days. Existing files are
never replaced. There is no rotation or append mode yet.

Each active log requests 64 KiB of PSRAM from the shared allocator; each terminal
requests a separate 4 KiB display queue. Three logs are the software maximum
(two UARTs are usable with the current LCD wiring). Buffers have no internal-RAM
fallback and are released when no longer needed. The capture and writer tasks
have fixed 4 KiB and 6 KiB internal OCRAM stacks plus small internal control
structures. Existing UART RX rings remain 4 KiB per port. No second PSRAM chip
or permanent PSRAM partition is needed.

Capture polls independently of the writer, which batches up to 1024 bytes per
port per tick and flushes files approximately once per second. File I/O does not
hold the capture lock. `serial stop` drains the remaining queue and closes the
file. Stop before ejecting the destination or powering off. Background logging
is system-wide: any console may inspect or stop it.

`serial status` reports RX/TX, queue usage, dropped log payload bytes, dropped
view bytes, stored file bytes, storage-lost bytes and errors. Timestamp overhead
consumes queue capacity. On a storage error, new log data stops being accepted;
RX/TX counters can still advance. UART hardware/driver overruns and framing or
parity errors are not exposed by the current core, so a zero software-drop
count is not a guarantee of lossless capture. SD latency and concurrent workloads
still require physical throughput testing.
