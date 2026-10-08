#!/usr/bin/env python3
"""Check the 2026-10-07 display profile over USB; physical pixels need user confirmation."""
import argparse
import json
import re
import time
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_ftp import Console

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--log', type=Path, required=True)
args = parser.parse_args()
report = dict(passed=False, commands=[])
ports = [p.device for p in list_ports.comports() if (p.vid, p.pid) == (0x16c0, 0x0483)]
assert len(ports) == 1, ports
with serial.Serial(ports[0], 115200, timeout=.03, write_timeout=3, exclusive=True) as usb:
    console = Console(usb, report)
    monitor = False
    try:
        console.command('')
        output = console.command('sessions')
        assert not re.search(r'\b(?:active|suspended)\s+', output), output
        pins = console.command('io pins')
        for pin, owner, label in ((10, 'primary-display', 'CS'),
                                  (14, 'primary-display', 'reset'),
                                  (15, 'primary-display', 'backlight'),
                                  (16, 'i2c1', 'SCL'), (17, 'i2c1', 'SDA'),
                                  (30, 'secondary-display', 'reset'),
                                  (31, 'secondary-display', 'DC'),
                                  (32, 'secondary-display', 'CS'),
                                  (33, 'secondary-display', 'backlight')):
            assert re.search(r'^\s*'+str(pin)+r'\s+'+owner+r'\s+'+label+r'\s*$', pins, re.M), pins
        assert re.search(r'^\s*37\s+free\s*$', pins, re.M), pins
        for pin in (10, 14, 15, 30, 31, 32, 33):
            assert f'gpio: pin {pin} busy:' in console.command(f'gpio mode {pin} out 0')
        console.command('lcd send "echo DISPLAY_WIRING_OK"')
        time.sleep(.5)
        assert 'DISPLAY_WIRING_OK' in console.command('lcd dump')
        console.command('lcd send "ltop"')
        monitor = True
        time.sleep(3)
        output = console.command('lcd dump')
        assert all(label in output for label in ('DTCM', 'OCRAM', 'PSRAM')), output
        console.command('lcd key q')
        monitor = False
        # Exercise RA8875 pixel transfers after the ST7735 initialized SPI0.
        # A static text dump alone cannot establish that graphics still work.
        for cycle in range(2):
            console.command('lcd send "invaders"')
            monitor = True
            time.sleep(2)
            output = console.command('lcd')
            assert 'graphics=active' in output, output
            frames = int(re.search(r'frames=(\d+)', output)[1])
            assert 'USB_DURING_GRAPHICS' in console.command('echo USB_DURING_GRAPHICS')
            time.sleep(2)
            output = console.command('lcd')
            assert int(re.search(r'frames=(\d+)', output)[1]) > frames, output
            console.command('lcd key exit')
            time.sleep(.5)
            assert 'graphics=idle' in console.command('lcd')
            monitor = False
        console.command('mem')
        console.command('audio status')
        report['passed'] = True
    finally:
        try:
            if monitor:
                console.command('lcd key exit')
        except Exception as error:
            report['passed'] = False
            report['cleanup_error'] = repr(error)
            raise
        finally:
            args.log.write_text(json.dumps(report, indent=2)+'\n')
print('PASS: display pin ownership, protected GPIOs, Wire1 preserved, LCD console/monitor and graphics/USB coexistence')
