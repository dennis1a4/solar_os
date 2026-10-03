SolarOS Teensy offline Python bundle, version 1

Firmware must include the resource-managed _solaros_hw adapter. The default
import path includes /flash/lib and /sd/lib after the script/current directory.
Libraries are loaded on demand into the 512 KiB PSRAM Python heap.

AVAILABLE
machine.Pin: pins 0..41, IN/OUT, PULL_UP/PULL_DOWN, value/on/off/deinit.
machine.I2C: bus 0 SDA18/SCL19, bus 1 SDA17/SCL16, bus 2 SDA25/SCL24.
  Fixed 100 kHz; max 32 bytes per direction. readfrom[_into], writeto,
  readfrom_mem[_into], writeto_mem; 8/16-bit register addresses. Each accessed
  device address is leased until deinit or script exit. scan skips reserved
  addresses. Arbitrary stop=False transactions are unsupported.
machine.SPI: SPI(1,cs=37) slot0, SPI(1,cs=36) slot1; SPI(0,cs=9) slot2 is
  unavailable on this bench because pin9 is display reset; slot0 is also
  blocked by the display CS37. Automatic CS per call, MSB, 8 bits,
  modes 0..3, max 12 MHz and 4096 bytes. Drivers needing manually held CS
  across several calls need adaptation. read/readinto/write/write_readinto.
machine.UART: Serial7 (RX28/TX29), Serial8 (RX34/TX35), Serial3 (RX15/TX14).
  Serial3 conflicts with the current LCD wiring. 300..1000000 baud, 8N1,
  read/readinto/readline/write/any, timeout 0..60000 ms, max 4096-byte call.
machine.ADC: analog-capable physical pins 14..27,38..41. read_u16 scales the
  native default 10-bit ADC result. Board-reserved pins are unavailable; pin14
  is available on this bench. Physical ADC tests remain pending. ADC is disabled
  in profiles with physical scope ADC enabled until controller arbitration exists.
machine.PWM: only unreserved timer pairs 28/29 or 36/37. Claims both pins to
  protect shared frequency. freq 1..100000 Hz, duty_u16 0..65535, deinit.
  Hardware frequency/duty verification remains pending.
All machine devices support with-statements. Use deinit to release promptly;
script exit, exception and cancellation release remaining native claims.
Pin passed to ADC/PWM or as SPI cs transfers its ownership and is deinitialized.

os: listdir/ilistdir, stat, mkdir, rmdir, remove/unlink, rename, getcwd/chdir.
  chdir changes the calling shell session directory. Paths use solarOS mounts.
  rename preserves an existing destination and raises EEXIST.
time: sleep/sleep_ms/sleep_us, ticks_ms/ticks_us/ticks_diff/ticks_add, time.
  time() uses Unix UTC seconds. Ticks wrap at 2**30. Sleeps are cooperative;
  sleep_us is not a hard-real-time timing guarantee. No localtime/mktime yet.
Utilities: heapq, bisect, itertools, functools, ucontextlib, contextlib subset.
Sensor sources: lsm9ds1 and bmm150 import-tested, physical hardware pending.

NOT INCLUDED AS WORKING APIs
Pin IRQs, software buses, arbitrary pin remapping, framebuf, asyncio, select,
standard ssl, OS processes, MIDI/audio/USB-PD/CAN Python bindings. Existing
solaros.gfx, solaros.net and TCP socket APIs remain available. This is a
compatibility subset, not a stock Teensy MicroPython firmware distribution.

OFFLINE SOURCE ARCHIVE
The SD /python-offline directory contains a pinned official micropython-lib ZIP.
Use `unzip -l /sd/python-offline/NAME.zip` to list it and the solarOS unzip
command to extract into a new directory. The archive includes untested and
incompatible packages; copying a package does not supply missing native APIs.
Keep dependencies and license files. Do not install every package over /flash/lib.
The installed manifest records source paths and SHA256 hashes. MIT and other
upstream licenses are in MICROPYTHON-LIB-LICENSE.txt and individual source files.

EXAMPLES
python /sd/python-examples/offline_check.py
python /sd/python-examples/scan_i2c.py       (only probe intended peripherals)
python /sd/python-examples/uart_echo.py     (3.3 V TTL interface required)
