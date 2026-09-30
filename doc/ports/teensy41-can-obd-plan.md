# CAN and OBD-II software plan — 2026-09-28

The first OBD/demo implementation is now complete and host-tested; see
[OBD implementation notes](teensy41-obd.md). Hardware and canmon are still
pending. The remainder of this document records the broader design. The user wants two CAN
channels, an OBD code reader/clearer and a traffic monitor with bidirectional
forwarding. Hardware is not built. The user confirmed MCP2515 over SPI, paired with
MCP2551; the original MCP25125 name was a typo. SPI is the controller interface,
not the slot UART. The pending SD recovery candidate remains unflashed.

## Applications and shared service

- `obd`: generic emissions OBD over Classical CAN/ISO-TP. Read stored (03),
  pending (07) and permanent (0A) DTCs, identify responding ECUs, show MIL and
  readiness, save a report. Explicit clear action (04) explains that clearing
  also resets diagnostic information/readiness; show per-ECU responses and
  reread rather than claiming success on transmit. Permanent DTCs cannot be
  manually erased. Manufacturer ABS/airbag/body diagnostics and older K-line/
  J1850 OBD are separate future work.
- `canmon`: one/two-channel timestamped frame history, ID filtering, latest
  payload and changed-byte display, rates, controller errors, dropped-frame
  counters and optional storage logging. Start listen-only. Explicit bridge
  mode forwards complete frames A-to-B and B-to-A. Later, add separately enabled
  match/drop/modify rules after unchanged forwarding is validated.
- Shared CAN service owns each controller, bounded RX/TX queues, mode and bitrate
  configuration, timestamps and error state. Exclusive transmit ownership keeps
  OBD requests from being injected into an active bridge unintentionally.
  Monitoring subscribers may share snapshots. Drivers are replaceable:
  simulated bus first, MCP2515 SPI, optional native FlexCAN.
- ISO-TP keeps independent state per ECU/address pair, validates lengths and
  sequence numbers, sends flow control, honors separation/block limits and
  bounds waits and memory. Raw forwarding does not terminate ISO-TP.
- A dedicated forwarding worker must not wait for LCD redraw or SD/USB writes.
  Logs/UI consume bounded copies; overflow is counted visibly. Bridge capacity,
  latency, arbitration changes and bus-off behavior need physical measurement;
  it is a frame gateway, not an electrically transparent wire.

## Hardware decisions

MCP2515 is an SPI Classical CAN controller, not a UART adapter; MCP2551 is a
physical transceiver. Each channel needs its own controller, transceiver, chip
select and preferably interrupt signal. Oscillator frequency and bus bitrate
must be explicit configuration, not inferred by transmitting probes. MCP2515
has only two RX buffers: interrupt servicing and short SPI transactions matter.
It cannot receive CAN FD frames.

The MCP2551 is a 5 V part. Review logic levels in both directions before wiring
to the 3.3 V Teensy. MCP2562's separate VIO is an alternative for a new design.
Teensy 4.1 already has three CAN controllers, so native CAN plus transceivers is
another option if the PCB exposes usable CAN pins. Do not assume existing slot
UART pins can be remapped to CAN.

The user clarified the intended allocation: slot 0 is reserved for the display;
slots 1 and 2 are expansion modules. Plan CAN A on slot 1 and CAN B on slot 2.

The current bring-up firmware does not yet match that allocation: board.h maps
slot 0 to SPI1 CS37, slot 1 to SPI1 CS36, and slot 2 to SPI0 CS9. The display
profile sets CS37/reset9, and sk_displays_begin unconditionally claims slot 2
as well as the slot matching the display CS. Thus GPIO9 is currently both LCD
reset and the mapped slot 2 chip select. Reconcile the actual connector wiring,
pin map and display claims before enabling CAN B; simply removing the slot 2
claim would leave that signal conflict. This is a bring-up configuration issue,
not a change to the user's intended slot roles. Do not change the running display
pin configuration without confirming the wiring.

Existing slot SPI operations support bus ownership, but their current lock
waits must be reviewed for forwarding latency. No automatic SPI probing.

For a bridge, A and B must connect to electrically separate CAN segments. Two
transceivers connected to the same OBD CAN pair do not intercept traffic.
An inline OBD extension can bridge tester-to-vehicle traffic; intercepting an
ECU branch requires access to that branch. A diagnostic gateway may restrict
what is visible at OBD. Termination depends on the topology; do not blindly add
two 120-ohm loads to an already terminated vehicle bus. Plan appropriate vehicle
power/input protection and a defined hardware bypass/failure behavior before
using an inline installation. Start forwarding validation on a bench network.

## Work possible without boards

1. Portable frame types, bounded capture/forward queues, DTC encoding/decoding,
   ISO-TP and OBD transaction state machines with deterministic host tests.
2. Simulated two-channel transport and ECU fixtures: multiple responders,
   multi-frame codes, sequence errors, timeout, clear accepted/rejected, queue
   overflow and bus-off. Test no transmit in listen-only mode and no unintended
   forwarding loops; do not deduplicate legitimate identical CAN frames.
3. Native `obd` and `canmon` apps backed by simulation, explicit demo labeling,
   LCD/serial rendering and log export. Keep background bus service independent
   of app drawing and storage latency.
4. Confirm oscillator clock, pins, voltage interface and interrupts; implement
   hardware driver and loopback checks. Physical acceptance then covers two
   independent buses, full-load bidirectional forwarding and fault handling.

## Primary references

- [Microchip MCP2515 datasheet](https://ww1.microchip.com/downloads/en/DeviceDoc/MCP2515-Stand-Alone-CAN-Controller-with-SPI-20001801J.pdf)
- [Microchip MCP2551](https://www.microchip.com/en-us/product/MCP2551)
- [Microchip MCP2561/2 datasheet](https://ww1.microchip.com/downloads/en/DeviceDoc/20005167C.pdf)
- [PJRC Teensy 4.1](https://www.pjrc.com/store/teensy41.html)
- [Linux ISO-TP implementation documentation](https://docs.kernel.org/networking/iso15765-2.html)
- [ELM Electronics OBD reference](https://elmelectronics.com/wp-content/uploads/2020/05/ELM327DSL.pdf)
- [BAR permanent DTC explanation](https://www.bar.ca.gov/obd-test-reference)
