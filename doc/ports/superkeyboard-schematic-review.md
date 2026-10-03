# SuperKeyboard schematic review — 2026-10-01

Reviewed the flash-drive project at `/run/media/dennis/32GB/Old Projects/SuperKeyboard/SuperKeyboardCPU/SuperKeyboardCPU_v2/SuperKeyboardCPU.kicad_sch`, including its 14-sheet hierarchy. This is the planned PCB, not the connected test system. No schematic or firmware was changed.

Method: KiCad XML netlist, ERC, rendered schematic inspection of the power/audio/motor/CAN/DMM circuits, and manufacturer datasheets. Exports are in `/tmp/superkeyboard-schematic-review/`. This is an initial electrical/connectivity review, not exhaustive component qualification or layout sign-off.

## Confirmed issues to resolve before layout

1. **Audio codec supply voltage.** U10 SGTL5000 pin 20 (VDDIO) connects directly to +5V. Pin 5 (VDDA) is fed from +5V through R9 (100 ohms), on the same net as U11 amplifier supply. A resistor is not a regulated supply. JP6 also offers +5V to the codec address pin. Redesign the codec supplies around valid voltages and separate the amplifier supply. SGTL5000 VDDA/VDDIO operation is limited to 3.6 V maximum. Select the exact U9 AP7313 output-voltage variant for VDDD. [NXP datasheet](https://cache.nxp.com/docs/en/data-sheet/SGTL5000.pdf).

2. **Buck regulator ground is disconnected.** U2 pins 1 and 9 connect only to each other on `Net-(U2-GND-Pad1)`, not GND. Connect both ground connections appropriately, including the exposed pad. [TI LMR33630 datasheet](https://www.ti.com/lit/ds/symlink/lmr33630.pdf).

3. **Two separate 3.3 V nets.** U8 supplies `+3.3V`, while most peripheral circuitry uses `+3V3`. The latter has no supply connection in the exported netlist. U1's exposed 3V3 connections do not feed it either. Decide which regulator powers each domain, then name/connect them explicitly; do not blindly parallel the external regulator with the Teensy's onboard regulator.

4. **Expansion raw power is disconnected.** `+VDC` connects to J26/J27/J29/J30/J31 and motor selector JP19, but not to the power-input circuit. TP2's value “VDC” does not create a net label. Define the intended expansion voltage and source before joining these: raw USB-PD/barrel voltage is not necessarily a fixed 12 V supply.

5. **Wrong LED buffer symbol pin number.** U6 is a custom SN74AHCT1G125 symbol with both GND and Y numbered 4. Actual GND is pin 3 and Y is pin 4. Correct the symbol and audit its footprint mapping before generating a board. [TI pinout](https://www.ti.com/lit/ds/symlink/sn74ahct1g125.pdf).

6. **Analog channels are joined; divider is disconnected.** Teensy GPIO38 and GPIO40 both connect to `analogIn`, although Pins_v3 assigns separate VIN-monitor and DMM functions. On the DMM sheet, the R1/R2 junction has no ADC connection; the `analogIn` label floats elsewhere. R1/R2 also have no resistance values. Split the channels, specify measurement ranges, calculate the dividers, and design input protection. Teensy ADC inputs accept only 0–3.3 V. [PJRC electrical limits](https://www.pjrc.com/store/teensy41.html).

7. **J20 USB-C port is incomplete.** CC1/CC2 are unconnected and VBUS is directly on system +5V. Specify its source/sink role and implement the appropriate CC termination/controller and power switching/isolation. The current drawing does not establish compliant attachment or safe simultaneous-source behavior. U4 LTC4412 and U5 TPS2116 are parked, unconnected circuits, so neither currently provides power arbitration. Also review Teensy VIN/VUSB isolation when external power and programming USB coexist. [TI USB-C guide](https://www.ti.com/lit/eb/slyy228/slyy228.pdf), [PJRC power information](https://www.pjrc.com/store/teensy41.html).

8. **CAN controller input levels are not guaranteed.** U19 MCP2515 runs at 5 V but SCK/SI/CS and RESET connect to Teensy signals without upward level translation. Its SPI input-high minimum is 0.7×VDD (3.5 V at 5 V), and RESET requires 0.85×VDD (4.25 V). R102/R103 and R106/R107 reduce output levels but do not solve the input direction. Choose suitable translation or redesign the controller/transceiver voltage interface. [Microchip electrical characteristics, Table 13-1](https://ww1.microchip.com/downloads/en/DeviceDoc/MCP2515-Family-Data-Sheet-DS20001801K.pdf).

## Integration decisions and incomplete details

- **Main display CS:** J31 pin 7 connects to Teensy GPIO9 via `slot2CS`. The spreadsheet/user clarification assigns this signal to backlight PWM. No separate main-display CS is identified by this header. Establish whether the display needs another CS signal or a deliberately fixed selection arrangement. GPIO14/15 are reset/WAIT in the planned assignment.
- **Alternative modules versus simultaneous peripherals:** `slot0CS` selects U18 RFM95, U19 MCP2515, and the nRF24 connector J24, as well as the slot connectors. This is viable as mutually exclusive slot-0 module designs. If populated together, provide independent chip selects and examine all shared control/output lines. Separate module schematics/projects would make assembly intent clearer.
- **Motor enable differs from the spreadsheet:** Teensy `motorEn` reaches J8 only. TB6612 STBY is driven by U21 PD3 and pulled up by R66. Decide whether this is intentionally controlled solely by the motor MCU or whether Teensy GPIO10 must have hardware shutdown authority.
- **Motor MCU ground jumper:** U21 GND pins 3 and 5 are both behind JP15; AGND pin 21 is permanently grounded. The drawing mentions an ATmega328PB substitution. Audit exact alternate-part pinouts and separate any optional pin-3 connection from the mandatory digital ground connection. Record the required assembly jumper state.
- **I2C pull-ups:** SCL1/SDA1 have no explicit pull-ups in this netlist. Specify board pull-ups to the intended logic rail or document reliance on fitted modules. Check the 5 V motor MCU's input thresholds and firmware pull-up configuration against the 3.3 V Teensy bus.
- **Power component specifications:** Finish capacitor values/voltage ratings, diode part numbers/current ratings, inductor saturation rating, fuse ratings, and U8 thermal budget. U8 dissipates `(5−3.3)×I`; at 0.8 A that is 1.36 W. F2's text describes a regulator rather than a fuse rating. C19 is still `C FF`. The annotation about a fixed-output “-5.0” regulator should be reconciled with the actual adjustable LMR33630 part and feedback network.
- **Connector/net naming:** `slot1CS1` on the encoder circuitry is a separate net from `slot1CS`. GPIO5/6 still carry `74595latch`/`74595data` in the schematic despite being unassigned in the spreadsheet. Reconcile the final netlist with Pins_v3 after circuit corrections.

## ERC interpretation and items not to misdiagnose

KiCad reported 402 violations; 284 are library-symbol mismatches. This is not 402 independent electrical faults. Audit custom symbols against manufacturer pinouts, resolve library provenance, and mark genuinely unused pins intentionally before using ERC as a release gate. Do not automatically replace all cached symbols from the installed libraries.

- U10 CPFILT is open, but that is correct when either VDDA or VDDIO exceeds 3.0 V within its valid operating range. Do **not** blindly add the capacitor flagged by ERC; NXP says it must not be fitted in that case.
- Headphone HP_VGND is kept separate from system GND, as required.
- MCP2515 unused TXnRTS inputs have internal pull-ups; their unconnected status alone is not a fault.
- `D+`/`D-` and `d+`/`d-` are separate USB nets. Similar-name warnings are not sufficient reason to merge them.

Recommended next pass: correct power/voltage and symbol faults, settle module population and display/control pin allocation, complete analog protection and interface specifications, then repeat ERC and netlist checks before layout.
