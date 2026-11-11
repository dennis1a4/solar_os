#!/usr/bin/env python3
"""Verify the saved flash timezone loads on boot without rewriting the setting."""
import argparse
import hashlib
import json
import os
import re
import time
import zlib
from pathlib import Path
from test_teensy41_clock import connect


def saved_timezone(console):
    output = console.command(
        '''python -c "d=open('/flash/.solar-settings/time.bin','rb').read();print('TZSETTINGS',d.hex())"''')
    match = re.search(r'^TZSETTINGS ([0-9a-f]+)$', output, re.M)
    assert match, output
    data = bytes.fromhex(match.group(1))
    assert int.from_bytes(data[8:12], 'little') == zlib.crc32(data[12:]), 'settings CRC'
    if data[:8] == b'SKNVS001':
        stride, value_offset = 82, 18
    else:
        assert data[:8] == b'SKNVS002', 'unknown settings format'
        stride, value_offset = 339, 19
    assert len(data) == 12 + 16 * stride, 'settings size'
    entries = {}
    for start in range(12, len(data), stride):
        entry = data[start:start + stride]
        if entry[0]:
            key = entry[:16].split(b'\0', 1)[0].decode()
            if key in ('tz_name', 'tz_posix'):
                assert entry[16] == 3, 'timezone entry must be a string'
                entries[key] = entry[value_offset:].split(b'\0', 1)[0].decode()
    return entries['tz_name'], entries['tz_posix'], hashlib.sha256(data).hexdigest()


def verify(console, expected, report):
    name, posix, _ = expected
    output = console.command('setterm timezone')
    assert f'timezone {name} ({posix}); RTC stays UTC' in output, output
    rtc = console.command('rtc')
    match = re.search(r'epoch=(\d+)', rtc)
    assert match, rtc
    os.environ['TZ'] = posix
    time.tzset()
    local = time.localtime(int(match.group(1)))
    seconds = local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec
    output = console.command('time')
    match = re.search(r'^(\d{2}):(\d{2}):(\d{2})$', output, re.M)
    assert match, output
    hour, minute, second = map(int, match.groups())
    delta = (hour * 3600 + minute * 60 + second - seconds) % 86400
    assert delta <= 3, (output, rtc, posix, delta)
    report['checks'].append('saved timezone and local clock match flash configuration')
    print(f'PASS: {name} ({posix}), local clock agrees with UTC RTC', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--reboots', type=int, choices=range(1, 6), default=2)
    args = parser.parse_args()
    report = dict(passed=False, commands=[], checks=[])
    console = None
    try:
        console = connect(report)
        expected = saved_timezone(console)
        report['saved'] = dict(name=expected[0], posix=expected[1], sha256=expected[2])
        verify(console, expected, report)
        for index in range(args.reboots):
            console.port.write(b'reboot\r')
            console.port.close()
            console = None
            time.sleep(3)
            console = connect(report)
            # Inspect the active timezone before any Python or settings write.
            verify(console, expected, report)
            assert saved_timezone(console) == expected, 'saved timezone file changed'
            print(f'PASS: reboot {index + 1}, saved file unchanged', flush=True)
        report['passed'] = True
    finally:
        if console:
            console.port.close()
        args.log.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
