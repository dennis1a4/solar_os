#!/usr/bin/env python3
"""Exercise wired ESP32 Wi-Fi discovery over SolarOS SPI, without pairing.

Keep ESP32 USB serial monitors closed: opening the port can reset some boards.
Leaves the bridge enabled. Does not join a Wi-Fi network or change audio routing.
"""
import argparse
import json
import re
from pathlib import Path

import serial
from serial.tools import list_ports
from test_teensy41_ftp import Console


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port')
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--scans', type=int, default=10)
    parser.add_argument('--status-checks', type=int, default=100)
    args = parser.parse_args()
    if args.scans < 1 or args.status_checks < 1:
        parser.error('counts must be positive')
    ports = [p.device for p in list_ports.comports()
             if (p.vid, p.pid) == (0x16c0, 0x0483)]
    port = args.port or (ports[0] if len(ports) == 1 else None)
    if not port:
        parser.error('specify --port when there is not exactly one Teensy')
    report = {'passed': False, 'commands': []}
    try:
        with serial.Serial(port, 115200, timeout=.03,
                           write_timeout=3, exclusive=True) as conn:
            console = Console(conn, report)
            console.command('')
            result = console.command('wbridge on')
            assert 'bridge: ok' in result, result

            def errors():
                result = console.command('wbridge status')
                match = re.search(r'errors=(\d+)', result)
                assert match, result
                return int(match[1])

            baseline = errors()
            report['initial_link_errors'] = baseline
            for i in range(args.scans):
                result = console.command('wifi scan')
                assert 'SSID' in result, result
                assert not any(x in result for x in
                               ('error', 'timed out', 'not ready')), result
                assert errors() == baseline, 'link error count increased'
                print(f'Wi-Fi scan {i + 1}/{args.scans} passed', flush=True)
            for _ in range(args.status_checks):
                assert errors() == baseline, 'link error count increased'
            result = console.command('mem')
            assert 'failures=0' in result, result
            console.command('network status')
            report['passed'] = True
    finally:
        args.log.write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: Wi-Fi discovery and repeated SPI status checks')


if __name__ == '__main__':
    main()
