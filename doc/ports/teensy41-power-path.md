# October 19 motherboard power path

Schematic source: saved `SuperKeyboardCPU.kicad_sch` and `Power.kicad_sch` in
`/home/dennis/Documents/SuperKeyboard/PCBProjects_2026-10-05/Motherboard`.
[Pins_v4.ods](hardware/Pins_v4.ods), also saved alongside the original `Pins_v3.ods`, updates the Teensy assignments and adds
`MCP23008_Power` and `Power_Hardware_v4` sheets. V3 is preserved. Export a KiCad XML
netlist and run `scripts/hardware/update_power_pins.py NETLIST SOURCE DESTINATION`
to reproduce the spreadsheet update (requires Python lxml).

## Firmware and commands

Build `pio run -e teensy41_power_board`. This extends the current display/test
profile with `SK_POWER_PATH=1`; it preserves the existing DMM and Wi-Fi work.
The module-board profile retains the previous direct GPIO5/6 USB power wiring.
The power-board profile instead reserves Teensy5 for MCP23008 INT_N, leaves 6
unused, frees the old relay22 and VIN38 assignments, and maps slot2 CS to10.
Both displays retain the established SPI0 test wiring; the schematic's small
display connector has no SPI data/clock pins, so this is not a claim that the
unwired connector supplies SPI automatically.

The new service starts automatically in its own task. Missing or nonresponsive
hardware reports **unconnected**, with no fabricated voltage, current or power.
Missing devices are retried about once a second; no address scan is performed.
The bus adapter serializes I2C transactions with other applications. The MCP and
INA addresses are reserved against arbitrary GPIO/I2C scripts. Snapshots on the
small display use the normal partial-row renderer; the display never performs
I2C transactions and does not take ownership away from an active DMM/app.

```
powerpath status
powerpath max 2000       # 2 A, nominal shunt; saves before programming hardware
powerpath max off        # restore full-scale shunt-overvoltage threshold
lcd small page power
usbpower status
usbpower on
usbpower off
usbpower clear
```

`max` accepts decimal **100..5000 mA**. The 5A command ceiling matches the input
fuse markings, and does not establish the continuous ratings of traces,
connectors, MOSFETs, shunt or regulators. Current is the total common-path current
through R13, not the host port alone or a PD contract limit. No custom threshold
is chosen automatically. The desired limit is saved in the `power_path/max_ma`
settings key; the page says `pending` until the INA237 configuration and SOVL
threshold have been read back. The value can be saved before hardware is fitted.
A settings error is reported as `NOT saved`; the desired value remains volatile.
Reconnection reapplies the desired threshold; USB host power is never restored
ON automatically. These settings do not program INA237 or PD controller NVM.

The power page shows common bus V, signed current mA, power W, the three input
VALID signals, PD set voltage/RDO current, host enable/fault/attach, and max-current
state. Use NES Left/Right to cycle five pages; Select returns to System. A common
bus measurement cannot provide the individual three input voltages. Input
priority is USB-PD > barrel DC > AUX; validity does not prove which MOSFET path
is conducting. USB EN is a command and SINK_N is attachment, not a measured host
VBUS voltage. A stale or failed measurement is invalidated rather than shown as
zero. Core driver and formatter have no heap or RTOS dependencies.

## Wiring from the netlist

All three I2C devices use **I2C1: SCL16 / SDA17**. Address defaults below assume
pads1-2 (GND) bridged on the address jumper footprints. Override the
`SK_POWER_MCP_ADDRESS`, `SK_POWER_INA_ADDRESS` and `SK_POWER_PD_ADDRESS` flags if
physical straps differ.

| MCP23008 U5 GPIO | Package pin | Function | Direction / polarity |
| --- | --- | --- | --- |
| GP0 | 10 | U4 LTC4417 VALID1_N (USB-PD / F3 / V1) | Input, low valid |
| GP1 | 11 | U4 VALID2_N (barrel J4 / F1 / V2) | Input, low valid |
| GP2 | 12 | U4 VALID3_N (switched AUX / F2 / V3) | Input, low valid |
| GP3 | 13 | U22 TPS25820 EN | Output, high on; R128100k pulldown |
| GP4 | 14 | U22 FAULT_N | Input, low fault; R12010k pullup |
| GP5 | 15 | U22 SINK_N | Input, low sink attached; R13010k pullup |
| GP6 | 16 | U13 INA237 ALERT_N + U7 LTC2954 KILL_N | Input normally; drive low only at final shutdown |
| GP7 | 17 | U7 LTC2954 INT_N | Input, low requests graceful shutdown |

U5 INT8 connects to Teensy5. IOCON configures open-drain INT; GPIO input changes
are captured, with 50ms polling as a fallback. INTCAP captures short fault/button
assertions when present in the first interrupt capture; it cannot guarantee
capture of every pulse behind an earlier pending event. TPS25820 hardware current
limiting remains independent of software. A captured/active host fault latches
the host OFF; `clear` requires an inactive fault and never enables it. EN is
initially low (OLAT before IODIR), including after an expander reset. Enabling
is refused while shutdown, fault latch or shared KILL are active. Unmount USB
storage before explicit host OFF.

MCP23008 defaults to0x20 (JP15/16/17 A0/1/2). INA237 defaults to0x40
(JP13 A1 / JP14 A0). STUSB4500 defaults to0x28 (JP4 A0 / JP5 A1).

## Current trip and shutdown

R13's schematic value is **10 mOhm**, treated as nominal rather than calibrated.
INA237 uses ADCRANGE0 (5uV/count), continuous shunt/bus/temperature conversions
(1052us each, averaging1), SHUNT_CAL8192 and CURRENT_LSB1mA. VBUS is3.125mV/count,
and POWER is0.2mW/count. SOVL counts = max_mA *2. The hardware alert compares
individual conversions, with active-low open-drain transparent behavior. No
conversion-ready alert is enabled: it would inadvertently shut the board down.
Measurement flags, calibration/configuration and threshold readback are checked;
reset, checksum, overflow, read failure or incomplete conversion invalidates data.
Software samples about10Hz while hardware comparisons continue independently.

**The shared ALERT_N/KILL_N net means overcurrent cuts the entire switched power
path directly. Filesystem/job cleanup cannot run first.** Firmware does not add
a debounce or promise a graceful overload shutdown. The user confirms that the
INA237 has a dedicated 3.3V regulator **before the LTC4417 MOSFETs**. As long as
that upstream supply remains alive, an already configured INA keeps measuring
and asserting ALERT independently of Teensy hang/reset, failure to boot, or
switched-system shutdown. This retained behavior needs physical verification.
If all input/INA power is removed, its volatile configuration is lost: the custom
threshold is not restored until firmware runs after the next cold start. `max off` restores
SOVL0x7fff; it does not bypass TPS25820 limiting, fuses, other INA thresholds or
LTC2954's hardware pushbutton timeout. The firmware never drives GP6 high against
the INA237 open-drain alert.

A normal button request uses SolarOS's existing graceful shutdown coordinator:
stop apps/jobs, drain logs, sync storage and handle an explicitly configured PD
service, then turn off the host and assert GP6 low through I2C. This occurs before
IRQ disable because Wire requires interrupts. Monitoring freezes once a KILL
attempt starts so it cannot reconfigure GP6 back to an input and release KILL.
A failed KILL is reported; `poweroff --check` never asserts KILL. The existing
Teensy SNVS emergency long hold remains available. External power bypasses,
including an uncut Teensy VUSB/VIN link, can prevent full physical power-off.

## USB-PD

TPS25820 is a **5V Type-C host/source**, not the input PD negotiator. Its CHG is
grounded, so firmware cannot select a higher current advertisement through it.
U3 STUSB4500 is the input PD sink. Boot observation only reads non-destructive
ID/attach/VBUS-ready/policy-engine/target-voltage/RDO registers. It never resets,
renegotiates or programs a higher voltage automatically, and it does not consume
receive messages or clear-on-read alerts. The display labels the VSEL_PDO target
as `PD set`; RDO operating current is not measured current. It requires attached,
VBUS-valid, PE_SNK_READY and stable non-mismatched RDO before displaying its mA;
otherwise the target is marked waiting. Detach/read failure clears the snapshot.
This passive observation is not the fresh protocol handshake used by requests.

The existing explicit `pd open i2c1 0x28 BOARD_MAX_MV BOARD_MAX_MA` and `pd request`
commands remain available. If that service owns this controller, the dashboard
copies its state instead of reading its registers. A transient resource claim
protects passive reads from concurrent negotiator/script ownership. There are
no new PD NVM writes, automatic high-voltage requests or inferred measured PD
input current. `pd open` still explicitly requests 5V discovery as documented in
[USB-PD power](teensy41-power.md).

## Schematic issues found while implementing

These are actual saved-netlist findings; the PCB files were not edited:

- U4 UV1/2/3 and OV1/2/3 are all marked unconnected. They require the intended
  voltage-window circuitry before VALID or path switching can work reliably.
  The user confirms these divider resistors are still being added.
- R13 is marked10mOhm but assigned a Sensirion SHT4x humidity-sensor DFN footprint.
  Confirm the actual four-terminal shunt part, symbol/Kelvin pad mapping and
  footprint before fabrication or trusting the current-trip scale.
- `+3.3V` (INA237/LTC2954 standby regulator) and `+3V3` (expander) are distinct
  nets. The user confirms the INA regulator is upstream of the LTC4417 MOSFETs;
  verify retained supply, power sequencing and pullups, including I2C/ALERT when
  the Teensy/expander rail is off. GP6 is never a push-pull high output.

References: [INA237 datasheet](https://www.ti.com/lit/ds/symlink/ina237.pdf),
[MCP23008 datasheet](https://ww1.microchip.com/downloads/aemDocuments/documents/APID/ProductDocuments/DataSheets/MCP23008-and-MCP23S08-Data-Sheet-DS20001919.pdf),
[LTC4417 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/4417f.pdf),
[LTC2954 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/2954fb.pdf),
[TPS25820 datasheet](https://www.ti.com/lit/ds/symlink/tps25820.pdf), and
[ST's STUSB4500 register definitions](https://github.com/usb-c/STUSB4500/blob/master/Firmware/Project/Inc/USB_PD_defines_STUSB-GEN1S.h)
and [target voltage/RDO example](https://github.com/usb-c/STUSB4500/blob/master/Firmware/Project/Src/USB_PD_core.c).

## Validation

`bash scripts/ports/test_teensy41_power_path_host.sh` exercises the actual driver
and formatter with ASan/UBSan: absent/incorrect devices, GPIO direction/polarity,
boot OFF, fault/capture latching, clear/enable separation, expander reset,
shared-line open-drain KILL, ADC conversion/calibration, signed current/power,
limits/readback errors, monitor reset/overflow/checksum/conversion-ready checks,
passive PD target/RDO/detach, and bounded display rows. Shutdown tests run with
and without the power PCB, including failed KILL and dry-run behavior. Existing
host USB/motor and dashboard navigation/partial-row regression checks also pass.
The ODS ZIP members and legacy32u4 sheet are verified preserved.

The PCB is not connected. Real voltage/current accuracy, physical current trips,
USB host power/fault behavior, LTC button/cutoff timing and real PD observation
remain hardware validation work after the schematic issues above are resolved.

On-device validation with the PCB absent: the page reports Meter, Inputs, PD and
USB host as unconnected, with `spi-misses=0` and 980 bytes of monitor stack
remaining. The 1234mA test limit persisted across reboot as pending; the original
custom limit (off) was restored afterward. The image uses 1,678,132 bytes flash,
448,992 bytes RAM1 and 256,120 bytes RAM2. Temporary logs/fixtures are removed.

Live acceptance command: `python3 scripts/ports/test_teensy41_power_path.py
--expect-unconnected --reboot-check` (single shell line). Passed strict numeric
limit parsing, saving a pending setting, restoring it after an acknowledged USB
reboot, restoring the original off setting, and reading back the actual cached
Power screen. The harness keeps DTR asserted until `rebooting` is received so
the reboot command is consumed before closing the USB port.

## Suggested R13 replacement

Use a **Vishay WSK2512R0100FEA**, 10mOhm, 1W, 1%, four-terminal Kelvin shunt,
if selecting this part family. The 0.5% `WSK2512R0100DEA` uses the same land
pattern. At 5A the nominal drop is50mV and dissipation0.25W; still check ambient
thermal derating and the complete board current rating.

The installed KiCad library contains:

`Resistor_SMD:R_Shunt_Vishay_WSK2512_6332Metric_T1.19mm`

The T1.19mm version covers the manufacturer's 5..200mOhm range, including10mOhm.
It is a four-pad footprint; the ordinary two-pad2512 footprint is not equivalent.
In this KiCad footprint pads1/4 are the large current terminals and pads2/3 are
the small sense terminals. One valid orientation is upstream current into pad4,
load current out of pad1, INA IN+ to small pad3, INA IN- to small pad2, and VBUS
sense to the load-side bus. Route each INA sense trace separately from the shunt
sense pad, without load current in those traces.

The saved schematic connects R13.4 to IN+/upstream bus, R13.2 to IN-, R13.3 to
VBUS, and R13.1 to the load bus. With this footprint, move **INA IN+ onto the
upstream sense pad3**, and move **VBUS onto the load-side bus** if measuring
load-side voltage. Confirm symbol pin numbers and rotation before routing;
simply assigning a new footprint does not establish correct Kelvin wiring.
The motherboard schematic/PCB has not been modified by this recommendation.

[DigiKey Canada — WSK2512R0100FEA](https://www.digikey.ca/en/products/detail/vishay-dale/WSK2512R0100FEA/12142494)
(cut-tape541-WSK2512R0100FEACT-ND) and
[0.5% WSK2512R0100DEA](https://www.digikey.ca/en/products/detail/vishay-dale/WSK2512R0100DEA/13237501)
are listed as out of stock/backorder when checked on2026-10-19. The manufacturer's
[datasheet](https://www.vishay.com/docs/30108/wsk2512.pdf) defines the land pattern,
resistance-dependent terminal dimensions and ratings. Recheck availability when
ordering; listed substitutes can have different pin counts and footprints.

Master physical test queue: [motherboard power-path tests](teensy41-test-checklist.md#motherboard-power-path--2026-10-19).
