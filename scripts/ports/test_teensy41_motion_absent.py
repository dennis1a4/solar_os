#!/usr/bin/env python3
"""Check Level/Compass missing-sensor behavior over USB with PCB sensors absent."""
import argparse
import json
import re
import time
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_ftp import Console


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', type=Path, required=True)
    args = parser.parse_args()
    report = dict(passed=False, commands=[], checks=[])
    try:
        ports = [p.device for p in list_ports.comports() if (p.vid, p.pid) == (0x16c0, 0x0483)]
        assert len(ports) == 1, ports
        with serial.Serial(ports[0], 115200, timeout=.03, write_timeout=3, exclusive=True) as usb:
            c = Console(usb, report)
            c.command('')
            assert 'imu0  lsm6dsox' in c.command('imu list')
            assert 'mag0  mmc5983ma' in c.command('mag list')
            endpoints = c.command('stream list')
            assert 'imu.imu0' in endpoints and 'mag.mag0' in endpoints, endpoints
            for endpoint, format_name in (('imu.imu0', 'imu-v1-le'), ('mag.mag0', 'mag-v1-le')):
                out = c.command('stream status ' + endpoint)
                assert format_name in out, out
            for command in ('imu sample imu0 1000', 'mag sample mag0 1000',
                            'stream sample imu.imu0', 'stream sample mag.mag0'):
                out = c.command(command)
                assert re.search(r'^(imu|mag|stream): (ESP_ERR_)?NOT_FOUND$', out, re.M), out
            report['checks'].append('shared IMU/magnetometer registration, stream metadata and missing-hardware sample errors before opening pages')
            c.command('lcd small off')
            try:
                for _ in range(4):
                    for endpoint in ('imu.imu0', 'mag.mag0'):
                        started = time.monotonic()
                        out = c.command('stream sample ' + endpoint)
                        assert re.search(r'^stream: (ESP_ERR_)?NOT_FOUND$', out, re.M), out
                        assert time.monotonic() - started < 2, out
                        out = c.command('stream status ' + endpoint)
                        assert 'handles=0' in out, out
                out = c.command('top')
                task = re.search(r'^motion\s+(run|ready|wait)\s+\d+\s+(\d+)\s+', out, re.M)
                assert task and int(task.group(2)) >= 512, out
                report['motion_stack_free_bytes'] = int(task.group(2))
                report['checks'].append('repeated failed stream reads release handles; motion task remains healthy with dashboard disabled')
            finally:
                c.command('lcd small on')
            for page, title, message in (
                    ('level', 'Digital level', 'IMU missing / stale'),
                    ('compass', 'Compass - magnetic', 'Magnetometer missing/stale')):
                out = c.command('lcd small page ' + page)
                assert 'unavailable' not in out, out
                previous = None
                for _ in range(3):
                    time.sleep(2.2)  # Cross independent device reprobe deadlines.
                    out = c.command('lcd small')
                    assert 'small=on' in out and title in out and message in out, out
                    assert 'Sensors disabled' not in out, out
                    assert 'nes-faults=0' in out and 'spi-misses=0' in out, out
                    match = re.search(r'updates=(\d+).*stack-free=(\d+).*nes-scans=(\d+)', out)
                    assert match, out
                    updates, stack, scans = map(int, match.groups())
                    assert stack >= 256, out
                    if previous:
                        assert updates > previous[0] and scans > previous[1], out
                    previous = updates, scans
                    print(page + ': ' + out.split('small=', 1)[1].splitlines()[0], flush=True)
                report['checks'].append(page + ': missing sensor reported; updates and NES scans progress through reprobes; no SPI/reader faults')
            c.command('lcd small page system')
            time.sleep(.3)
            out = c.command('lcd small')
            assert 'Digital level' not in out and 'Compass - magnetic' not in out, out
            assert 'small=on' in out, out
            c.command('uptime')
            report['checks'].append('returned to System; USB shell responsive')
        report['passed'] = True
        print('PASS: missing IMU/magnetometer pages, retry responsiveness, dashboard restoration', flush=True)
    finally:
        args.log.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
