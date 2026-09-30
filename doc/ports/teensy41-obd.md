# OBD app and simulated CAN foundation — 2026-09-28

Implemented and host-tested; **installed with the clock image on 2026-09-28**.
On-device OBD demo testing remains pending. No vehicle or MCP2515 has been connected or
validated. `canmon` remains the next separate app.

## Try it

```text
obd --demo
obd --demo --scenario timeout
obd --demo --scenario sequence
obd --demo --scenario reject --report /sd/obd-demo-report.txt
```

`obd` without `--demo` explicitly reports that a hardware driver is not installed.
It cannot silently use a vehicle connection. The title and exported reports say
DEMO. Neither startup nor scan accesses SPI, GPIO or a physical CAN channel.
Two simulated ECUs reply at 7E8/7E9. The normal scan takes approximately six
seconds and reads service 01 PID 01, stored (03), pending (07) and permanent (0A)
codes. Four engine codes force an actual multi-frame ISO-TP response.

Controls: Tab cycles eight ECU addresses; 1/2/3 changes code category; arrows
scroll; R rescans; C opens a clear confirmation; uppercase Y confirms; N/Escape
cancels; S saves; Q/Escape/Ctrl-] quits. Each service has independent validity:
missing or malformed replies show unavailable, never an empty successful list.
MIL, reported stored count and readiness support/incomplete counts are displayed.
Reports include raw readiness bytes, per-category negative response codes and
clear acknowledgement/timeout/rejection. Clear targets only the selected ECU,
waits for its reply, then rescans. Simulation retains permanent codes and resets
readiness after clear. Restarting the app restores all fixtures.

Reports use exclusive creation at `--report` or a generated
`/sd/obd-demo-TIME-N.txt` path. Partial scans are labeled in progress; storage
errors are displayed and partial files may remain after a write failure.

## Architecture

- `solar_os_can`: Classical CAN frame validation, two-channel metadata, bounded
  32-frame RX/TX queues, explicit channel enable and listen-only transmit refusal.
  Full queues reject/count dropped frames. No global bus state or automatic
  discovery. Currently single-owner service instances; a future hardware worker
  must supply locking and exclusive channel ownership for multiple consumers.
- `solar_os_isotp`: normal-addressed Classical CAN single/first/consecutive/flow
  control frames, independent TX/RX buffers and timers, sequence checking,
  bounded WAIT handling, sender block-size/STmin behavior and 1-second timeouts.
  256-byte payload cap; oversized first frames receive overflow flow control.
  Transport accepts configured 11/29-bit IDs; sub-millisecond STmin is rounded
  up to one millisecond. It is a bounded first implementation, not a claim of
  complete ISO certification or support for every optional addressing mode.
- `solar_os_obd`: eight separate transport contexts for 7E8–7EF, functional
  queries on 7DF and physical flow-control/clear requests on 7E0–7E7. Strict
  service/length/count validation, DTC string decoding, 32 codes/category/ECU,
  1.5-second collection window per service. Results and counters are per instance.
- `solar_os_obd_demo`: two ECUs consume the same outgoing CAN frames and produce
  replies through the same ISO-TP implementation, including flow control. Timeout
  suppresses responses; sequence corrupts consecutive-frame sequence numbers;
  reject returns NRC 22 for clear. No direct mutation of client scan results.
- `solar_os_obd_app`: native text TUI on serial or LCD; cold app state allocated
  in external memory by the shared lifecycle. CAN and ISO-TP processing is
  tick-driven for simulation. Real-time forwarding is not implemented here.

MCP2515 hardware backend, bitrate/oscillator configuration, bus-off recovery,
physical channel ownership, 29-bit OBD discovery, response-pending timing,
manufacturer-specific diagnostics, DTC description database, CAN FD and `canmon`
are future work. OBD NRCs are surfaced as received; extended pending-response
sessions are not supported. Once hardware exists, validate interoperability
against an independent ECU/test tool; the simulator shares transport code and
cannot independently establish protocol conformance.

The user's intended PCB roles are display in slot 0, CAN A in slot 1, CAN B in
slot 2. Temporary test wiring conflicts do not change that allocation. No pin
map/display wiring was changed. See [hardware plan](teensy41-can-obd-plan.md).

## Validation

```sh
bash scripts/ports/test_teensy41_obd_host.sh
pio run -e teensy41_display
```

The host script builds protocol/model and actual app lifecycle tests with
AddressSanitizer/UndefinedBehaviorSanitizer and `-Wall -Wextra -Werror`.
Coverage: queue bounds/listen-only, maximum-sized ISO-TP/sequence wrap, flow
control and separation, WAIT/overflow, malformed frames, deadlines across clock
wrap, interleaved ECUs, scan success/timeout/corruption, accepted/rejected/silent
clear with rescan, permanent-code persistence, selected-ECU isolation, report
contents and no-overwrite, confirmation cancel and lowercase rejection, resized
serial geometry and app cleanup. No physical validation is implied.

Manual generator: 14 tests passed. Package configuration: 29 tests passed.
The repository-wide app-state policy check has three pre-existing violations
in unchanged `src/platform/imxrt1062/teensy41/python.c` (source_len/line_start,
initialized and branches). The new OBD app passes the policy in isolation;
the other two lifecycle-policy tests pass. Do not mark the full policy suite green.

## References used during implementation

[ELM Electronics OBD reference](https://www.elmelectronics.com/wp-content/uploads/2017/01/ELM327DS.pdf)
explains the extra DTC count byte in CAN OBD replies and DTC encoding.
[Linux ISO-TP documentation](https://docs.kernel.org/networking/iso15765-2.html)
describes transport frames, flow control and separation-time encoding.
Hardware selection remains [MCP2515](https://www.microchip.com/en-us/product/MCP2515)
over SPI, with transceiver voltage/pin details to be finalized before assembly.
