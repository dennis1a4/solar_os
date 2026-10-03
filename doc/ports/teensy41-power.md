# USB-PD power app — 2026-09-28

Native text `pdpower --demo` app and portable negotiation policy implemented.
The upstream integration branch adds a portable STUSB4500 backend and explicit
`pd` controller commands. Hardware is not wired and remains unvalidated; no
controller starts automatically. The `pdpower --demo` UI remains a simulator. The demo is now installed with the Telnet legacy
image; power-app on-device interaction tests remain pending.

## Use

Run `pdpower --demo` from either the LCD shell or a USB serial terminal after
installing the new display build. Use 1–4 to select 5, 9, 15 or 20 V; +/- changes
requested current in 100 mA steps (100–3000 mA). Enter opens confirmation;
uppercase Y requests the selection, N cancels. Q/Escape exits. Every app launch
starts a fresh simulated source at 5 V / 1000 mA. No settings survive exit.

`--scenario normal|reject|timeout|disconnect|io-error|mismatch` selects the
simulated request outcome. Example: `pdpower --demo --scenario timeout`.
The display distinguishes a pending request, confirmed contract, failure and
unavailable contract. Contract current is not actual consumption. The demo
limits and offered profiles are examples, not the electrical ratings of the PCB.
The UI requires at least 16 terminal rows; narrower text is clipped to fit.

## Scope and architecture

- App state is allocated only during the session in PSRAM, using the existing
  app lifecycle. Service/model uses caller-owned bounded storage; no heap use.
- `solar_os_pd_power.*` is separate from the existing system power/sleep service.
- Backend operations are request and poll. Policy rejects requests beyond source
  capabilities, board voltage/current limits, fixed-PDO encoding limits and
  overlapping requests. Completion must identify a fresh, matching contract.
- Three-second negotiation deadline uses unsigned elapsed time (tick-wrap safe).
  Detach and communication errors invalidate source/contract information.
- Simulation never accesses I2C, GPIO, NVM, motor controllers or load switches.
  `pdpower` without `--demo` reports that the hardware backend is unconfigured.
- No Python binding or nonvolatile save command is included in this version.

## Hardware follow-up

User plans STUSB4500QTR, back-to-back input MOSFETs, 3.3 V and 5 V regulators,
and a branch of negotiated input power feeding a future motor controller.
Motor control is explicitly deferred. I2C bus, address straps, optional ALERT
pin and board voltage/current ratings are still unspecified.

Follow ST's [UM2650 programming guide](https://www.st.com/resource/en/user_manual/um2650-the-stusb4500-software-programing-guide-stmicroelectronics.pdf):
configure volatile sink PDOs, retain mandatory 5 V PDO1, and send PD Soft Reset
to renegotiate. Capture source capabilities promptly from receive buffers;
these are transient, not a permanently readable list. Treat a register write
as a request only; verify fresh protocol completion and the selected source
profile before reporting success. The source's profile index is not the sink's
profile index. Do not infer measured current from the RDO.

Implement the real backend through the existing locked Teensy I2C transport
once wiring is established. Review alert servicing latency, I2C timeout behavior,
source-capability cache invalidation on reconnect, voltage transition handling,
startup at 5 V, and regulator input limits with the actual board. Hardware state
must outlive app exit, unlike the deliberately per-session demo. Keep controller
NVM programming a separate explicit feature; no automatic restore/retry of a
high-voltage request on boot or reconnect is implemented here.

## Validation

`bash scripts/ports/test_teensy41_power_host.sh` runs the actual model and app
callbacks under AddressSanitizer/UndefinedBehaviorSanitizer. Covers current and
voltage limits, malformed capability count, missing backend, overlapping requests,
tick wrap, successful completion, rejection, timeout, detach, I/O failure,
mismatch, uppercase confirmation/cancel, small terminal clipping and lifecycle.
All passed, along with 14 manual-generator tests. `teensy41_display` builds
successfully; evidence: `/tmp/teensy-power-build.log`. Flash usage is 1,269,676
bytes (15.6%), an increase of 5,016 bytes over the Clock image. Reported static
RAM allocations are unchanged: RAM1 435,360 and RAM2 225,856 bytes. Runtime
heap/PSRAM has not been measured for this app on the board.
No real PD hardware or LCD visual testing has been performed.


## STUSB4500 software backend (integration branch)

`pd status` is read-only. After wiring and limits are verified, use
`pd open i2cN ADDRESS BOARD_MAX_MV BOARD_MAX_MA` to claim the address and start
5 V / 100 mA discovery. `pd request MV MA` requires an advertised fixed source
profile and enforces the supplied board limits. `pd status` reports the result.
`pd close` releases the service but does not change the negotiated voltage.
Configuration is not saved. No NVM access, automatic high-voltage selection,
measured current, load switching or motor control is implemented.

The backend checks fresh Source_Capabilities, Accept and PS_RDY observations,
then the source-indexed RDO and ready state. Register reads/writes use the locked
Teensy I2C adapter; failures invalidate cached contract information. Host mock
checks cover fresh/stale observations, source-index mapping, limits, failures,
timeout and detach. Hardware tests are listed in the master checklist.
Register references: [ST reference implementation](https://github.com/usb-c/STUSB4500/blob/master/Firmware/Project/Src/USB_PD_core.c)
and [register definitions](https://github.com/usb-c/STUSB4500/blob/master/Firmware/Project/Inc/USB_PD_defines_STUSB-GEN1S.h).
