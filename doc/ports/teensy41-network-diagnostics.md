# Teensy network diagnostics and time synchronization

Stage 3 of the approved workstation sequence adds `ping`, `netscan` and `ntp`
to the display-derived firmware profiles. Start native Ethernet first:

```text
network up
network status
ping 192.168.1.1
netscan 192.168.1.1 22,80,443
ntp -q
ntp
```

`ping HOST [COUNT]` sends four ICMP echo requests by default, or 1–999 when
specified. Requests carry 16 payload bytes and use a one-second reply deadline
and interval. Replies include sequence, TTL and elapsed time; the summary shows
loss and minimum/average/maximum timing. Only one probe is admitted at a time.
Another console gets a busy result instead of replacing its owner’s probe.

`netscan HOST|A.B.C.D-E|CIDR [PORTS]` performs sequential TCP connection probes.
CIDR sizes are /24 through /32; /24–/30 omit network/broadcast addresses, while
/31 and /32 retain all addresses. A last-octet range includes both ends. Ports
are comma-separated numbers or ranges, deduplicated, with at most 128 distinct
ports. Defaults: 22,23,53,80,443,1883,8080. Each connection has a 350 ms deadline.
Only successful connections are reported as open; absence from the output does
not distinguish closed, filtered, slow, or immediately disconnecting services.
This is a bounded TCP reachability tool, not service identification or a SYN
scanner. A service that closes before the transport observes its connection can
be missed. Use explicit targets/ports when checking your own equipment.

`ntp [-q] [SERVER [PORT]]` defaults to `pool.ntp.org:123`. `-q` reports the server
UTC epoch without changing the RTC. Otherwise a valid reply sets the hardware
RTC in UTC and displays local time using the existing timezone; timezone settings
are not changed. The optional port supports local time servers and test fixtures.
This is a one-shot synchronization, not a periodically running NTP discipline.

NTP replies must match the resolved source address/port and random request nonce,
use server mode/version 3 or 4, report a synchronized nonzero stratum, and contain
plausibly ordered receive/transmit timestamps. Denials, truncated packets and
out-of-range dates cannot update the clock. The supported UTC range is 2000
through the 32-bit RTC limit in 2106, including the 2036 NTP era rollover. A query
waits at most five seconds after UDP setup; DNS has a five-second bound and nonce
collection a one-second bound. This is ordinary unauthenticated NTP: use a trusted
server; packet validation does not provide cryptographic authentication.

All three commands support Ctrl+C/Escape and yield the console so other consoles
and background jobs can run. The Ethernet task owns ICMP objects, UDP/TCP state
and random-source access. Shell adapters use the existing shared network session
admission/cancellation and transport RPCs. Link loss invalidates active requests;
cleanup releases their handles before reuse. These are foreground commands, not
new retained applications or process jobs.

## Validation

```sh
bash scripts/ports/test_teensy41_netdiag_host.sh
python3 scripts/ports/test_teensy41_netdiag.py --log /tmp/teensy-netdiag-device.json
```

The host suite uses ASan/UBSan to check target/port bounds and NTP response
validation, timestamp ordering, era rollover and RTC overflow. The device suite
uses local TCP/UDP fixtures, requires an idle keyboard/exclusive USB, and sets
the RTC to the computer’s current UTC time. It tests ICMP replies, open/closed
ports, query-only and synchronized time, malformed/mismatched/denied replies,
cancellation, repeated memory recovery, competing LCD/USB probes and link restart.
It leaves Ethernet up and does not scan the LAN or alter timezone preferences.

Final installed-image acceptance passed on 2026-09-30. Five repeated diagnostic
cycles recovered exactly 29,124 internal / 8,163,984 PSRAM bytes free. The final
HEX SHA256 is `0558f79641d24ea48c8a6f697903f5b336f4712872443e407cb4fc342a445743`.
Evidence and matching source/firmware are preserved in
`../solar_os-baselines/2026-09-30-netdiag/`.
