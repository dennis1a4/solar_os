# Teensy DNP3 lab

The `teensy41_power_board` build enables `SK_DNP3`. See
[the app manual](../manual/apps.md#dnp3) for settings, commands, supported protocol
objects and limitations, and [the test checklist](teensy41-test-checklist.md#dnp3-lab-power-board-build)
for physical validation still required.

`dnp3` starts stopped; `dnp3 --demo` is an internal master/outstation loopback.
A is master-facing and B outstation-facing. Bridge uses both, master uses B and
outstation uses A. Each endpoint independently selects UART/baud/framing, TCP
listen/port or TCP connect/host/port. UART7/8 use existing expansion-slot leases;
UART3 conflicts with the main LCD. TCP listeners share the two dynamic listener
slots with FTP. No UDP transport or Wi-Fi-bridge socket backend is selected.

The portable lab owns a 4096-byte ingress buffer and 32 bounded output packets
per direction. The worker accepts only available input space, preserves partial
writes and serial/TCP backpressure, and drains TCP half-close independently in
both directions. Default bridge forwarding preserves arbitrary bytes immediately;
active mutation/fault modes frame traffic and release an incomplete candidate
unchanged after one second. RX decoding precedes mutation; completed writes are
recorded separately. Invalid input cannot cause an object edit.

App state, history, service buffers and the 12 KiB worker stack live in PSRAM.
The linker places DNP3 code in cached flash and retains the existing RAM startup
guard. A separate worker keeps transport running while the TUI is suspended;
background app ticks drain the optional TSV file log. Display/file writes occur
outside the lab mutex. Closing waits for I/O cleanup before freeing state.

The OpenDNP3-derived C decoder is bounded and carries its upstream license in
`src/vendor/opendnp3`. The lab layer implements its own limited encoder,
simulator and rewrite engine; this is not the complete OpenDNP3 stack. Supported
single-frame application edits retain object encodings, rebuild CRCs, and skip
unsupported/fragmented/authenticated payloads rather than guessing. Rule counters
reflect edits actually emitted. Simulated controls change the point database,
not physical GPIO outputs. No real output mapping is configured.

Host tests use an independent bitwise CRC encoder and Address/UndefinedBehavior
Sanitizer. Device acceptance uses two local test sockets for bridge checks and an
independent Python peer for master/outstation checks. Physical serial bandwidth,
transceiver wiring and third-party conformance remain manual acceptance items.

Validated on 2026-10-26: sanitizer core/app tests, power-board firmware build,
and live LCD/USB acceptance with independent Python TCP peers. Live checks cover
transparent bidirectional forwarding, control-index and BI/AI rewrites with CRC
rebuild, CRC fault injection, half-close drain, saved profiles, SD audit logging,
retained sessions, master TCP connect and outstation TCP listen with direct/SBO
commands. The small 26x16 layout is covered by host rendering tests. Physical
UART/transceiver and independent-stack serial interoperability remain pending.

Shared-network regression also passed the existing live FTP suite: binary
PASV/EPSV transfers, file operations, stalled-upload cleanup, ten start/transfer/
stop cycles with exact heap recovery, and Ethernet down/up recovery. The demo
ran through 1,110 scans without timeout/error; its worker retained 8,880 bytes
of stack headroom when stopped. The linked image uses 1,718,916 bytes of flash,
449,184 bytes RAM1 and 256,152 bytes RAM2; the startup RAM guard is unchanged.
