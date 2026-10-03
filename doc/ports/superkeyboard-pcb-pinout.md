# SuperKeyboard revised PCB pinout

Verified against `Pins_v3.ods`, sheet `T4_1`, on 2026-10-01:
`/run/media/dennis/32GB/Old Projects/SuperKeyboard/Pins_v3.ods`.

Spreadsheet SHA256: `d067548a80dba301ca01375fe94b2f0ba21ad6f9fb3394401bf94d7972ec4a5d`.

Dennis explicitly identified these assignments as the **PCB target**,
not the connected test system. This supersedes earlier proposed PCB assignments
for RGB/AmpEn/analog pins. It does not authorize changing the installed bench
profile or imply that this routing has been electrically validated.

The bench remains `teensy41_telnet_legacy`, with AmpEn40, Serial1 on 0/1,
primary display CS37/reset9/WAIT15, and physical scope ADC disabled. See
[installed image and handover](teensy41-handoff.md).

Slot 2 is a **dedicated main-display header**, using the standard expansion
connector pinout. It is not a general-purpose expansion slot. Names such as
`slot2CS` identify connector positions and must not override the listed signal
function. Dennis also corrected an accidental Motor-column mark associated
with the secondary backlight; do not interpret it as deliberate signal sharing.

## PCB signal assignments

| Pin | Function supplied | KiCad name supplied | Connection / notes |
| --- | --- | --- | --- |
| 0 | RGB LED WS2812B | na | ODS net-name cell is `na`; RGB_LED was in the pasted CSV. Replaces previous AmpEn0 proposal. |
| 1 | Amp Enable | ampEn | Audio amplifier; conflicts with Serial1 TX |
| 2 | 74595/NES Clock | shiftClk | Shared shift/NES clock |
| 3 | NES Latch | NESlatch | NES |
| 4 | NES Data | NESdata | NES |
| 5 | Unassigned | — | Previously shift latch in firmware; PCB assignment blank |
| 6 | Unassigned | — | Previously shift data in firmware; PCB assignment blank |
| 7 | Audio Data Out | audioOut | Audio |
| 8 | Audio Data In | audioIn | Audio |
| 9 | Display Light PWM | slot2CS | Main-display backlight PWM; net name denotes standard header position |
| 10 | Motor Driver Standby | motorEn | Motor |
| 11 | MOSI Display | slot2MOSI | SPI0, main display |
| 12 | MISO Display | slot2MISO | SPI0, main display |
| 13 | (LED) SCK Display | slot2SCK | SPI0, main display |
| 14 | Display port TX3 (reset) | slot2TX3 | Main display reset; UART3 TX alternate use |
| 15 | Display port RX3 (wait) | slot2RX3 | Main display WAIT; UART3 RX alternate use |
| 16 | SCL1 | SCL1 | Slot 1, second display, motor and USB-C controller marks |
| 17 | SDA1 | SDA1 | Slot 1, second display, motor and USB-C controller marks |
| 18 | Audio I2C | audioI2Ca | Existing firmware maps SDA here |
| 19 | Audio I2C | audioI2Cb | Existing firmware maps SCL here |
| 20 | Audio L/R clock | audioLRclk | Audio |
| 21 | Audio bit clock | audioBitClk | Audio |
| 22 | Relay | relay | Relay |
| 23 | Audio master clock | audioMasterClk | Audio |
| 24 | SCL2 | SCL2 | Slot 0 and display slot 2 |
| 25 | SDA2 | SDA2 | Slot 0 and display slot 2 |
| 26 | MOSI1 | slot01MOSI1 | SPI1, slots 0/1; second display per bus note |
| 27 | SCK1 | slot01SCK1 | SPI1, slots 0/1; second display per bus note |
| 28 | RX7 Slot 0 | slot0RX7 | UART7 RX |
| 29 | TX7 Slot 0 | slot0TX7 | UART7 TX |
| 30 | Second Display Reset | displayReset | Second display |
| 31 | Second Display Command | displayCMD | Second display D/C |
| 32 | Second Display CS | displayCS | Second display chip-select |
| 33 | Second Display Backlight | displayPWM | Dedicated backlight; extra Motor mark in saved ODS was identified by Dennis as corrected |
| 34 | RX8 Slot 1 | slot1RX8 | UART8 RX |
| 35 | TX8 Slot 1 | slot1TX8 | UART8 TX |
| 36 | CS1 Slot 1 | slot1CS1 | Slot 1 SPI1 chip-select |
| 37 | CS1 Slot 0/3 | slot0CS1 | Slot 0/3 SPI1 chip-select; meaning of slot 3 not specified |
| 38 | VIN monitor (with jumper) | analogIn | VIN monitoring |
| 39 | MISO1 | slot01MISO1 | SPI1, slots 0/1; second display per bus note |
| 40 | Analog input DMM | analogIn | DMM input; existing scope adapter may be reusable |
| 41 | Volume pot | volume | Audio |
| 42–47 | SD card | — | Internal SDIO |
| 48–54 | QSPI | — | Internal flash/PSRAM connections |

Slot 0 and slot 1 expose +12 V (Vin), +5 V, +3.3 V and GND in the supplied
connector table. These power rails are connector supplies, not GPIO signal levels.

**Bus allocation supplied:** SPI0 for the main display; SPI1 for the second
display and everything else. I2C1 serves slot 1 and the peripherals marked above;
I2C2 serves slot 0 and display slot 2. Audio retains its dedicated I2C bus.

## Confirmations and remaining integration details

- Pin 9 is backlight PWM. `slot2CS` is a standard-header reference, not evidence
  that the firmware should drive pin 9 as SPI chip-select. The sheet does not
  separately identify the main display's actual chip-select wiring; that detail
  belongs in the eventual display-specific driver/profile configuration.
- Pin 33 is secondary-display backlight PWM. The saved ODS has an extra mark in
  column L (Motor), not column M (USB-C). Dennis says the erroneous Motor mark
  was corrected. Treat it as a stale spreadsheet annotation, not a shared pin.
- Pins 38 and 40 both have `analogIn` in the saved ODS's KiCad-name column.
  Keep VIN monitoring and DMM as separate software roles; verify actual net
  names/connectivity against the schematic before implementing a PCB profile.
- Pins 5/6 are blank in the ODS. Pin 2 still mentions the 74595; latch/data
  removal or relocation is not specified by this sheet.
- Pin 37's text says slot 0/3, but only slot 0 is marked. No additional logical
  slot 3 is introduced on the basis of that label.

## Firmware implications, not yet applied

Create a distinct PCB profile once display-specific wiring is established.
It should reserve RGB0 and AmpEn1, disable Serial1, and reserve pin 40 for the
chosen DMM/ADC service. The current guard only checks AmpEn against Serial1 RX0;
it must also cover TX1 and the RGB reservation when adding the PCB profile.

Pin 9 needs a backlight role rather than its old expansion-CS role. Model
header 2 as display-only; do not offer its pins through generic expansion
claim/transfer commands. Update the main display's CS/reset assignments and
the slot/resource descriptors together. UART3 remains unavailable while
14/15 are used for display reset/WAIT. Slots 0 and 1 retain UART7 and UART8.
Whether CS37 becomes available depends on the final main-display CS decision.

Stop reserving/driving 5/6 only after confirming their absence from PCB functions.
Audit second-display SPI1 arbitration, per-device chip-select ownership, and
shared I2C addresses for the motor/USB-C peripherals. Existing GPIO/bus commands
can then use the PCB's reservations automatically; the commands themselves do
not need a separate PCB syntax.

No PCB firmware profile, pin remapping, wiring change or flash has been performed
as part of recording this pinout.
