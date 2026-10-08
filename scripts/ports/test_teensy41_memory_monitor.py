#!/usr/bin/env python3
"""Live RAM monitor checks (pyserial/pyte); requires idle consoles.

Restores terminal size and removes its unique temporary RAMFS mount.
"""
import argparse
import json
import re
import time
import uuid
from pathlib import Path

import pyte
import serial
from serial.tools import list_ports
from test_teensy41_ftp import Console


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--music', help='Optional existing music directory for playback coexistence')
    args = parser.parse_args()
    report = dict(passed=False, commands=[], screens=[])
    ports = [p.device for p in list_ports.comports() if (p.vid, p.pid) == (0x16c0, 0x0483)]
    assert len(ports) == 1, ports
    mount = '/mon' + uuid.uuid4().hex[:8]
    mounted = active = False
    geometry = None
    player = lcd_monitor = None
    with serial.Serial(ports[0], 115200, timeout=.03, write_timeout=3, exclusive=True) as usb:
        console = Console(usb, report)

        def memory():
            output = console.command('mem')
            regions = {}
            for name, used, free, capacity, minimum in re.findall(
                    r'(DTCM|OCRAM|PSRAM) \([^\n]+?\): used=(\d+) free=(\d+) capacity=(\d+) sampled-min=(\d+)', output):
                used, free, capacity, minimum = map(int, (used, free, capacity, minimum))
                assert used + free == capacity and minimum <= free
                regions[name] = dict(free=free, capacity=capacity, minimum=minimum)
            assert len(regions) == 3, output
            assert 'excludes direct malloc/new' in output and 'contiguous' in output
            return regions

        def monitor(cols, rows, small=False, resize=True):
            nonlocal active
            if resize:
                console.command(f'setterm size {cols} {rows}')
            screen = pyte.Screen(cols, rows)
            stream = pyte.Stream(screen)
            usb.write(b'ltop\r')
            active = True
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                stream.feed(usb.read(16384).decode(errors='replace'))
            display = '\n'.join(screen.display)
            report['screens'].append(dict(cols=cols, rows=rows, display=display))
            if small:
                assert 'small' in display.lower() or 'resize' in display.lower(), display
            else:
                for label in ('DTCM', 'OCRAM', 'PSRAM', 'Alloc fail=', 'TASK', 'FREE'):
                    assert label in display, (label, display)
                assert 'free ' in display if cols >= 40 else 'F:' in display
            console.command('q')
            active = False
            print(f'PASS: ltop {cols}x{rows}', flush=True)

        try:
            console.command('')
            assert not re.search(r'\b(?:active|suspended)\s+', console.command('sessions'))
            match = re.search(r'size (\d+) (\d+);', console.command('setterm'))
            assert match
            geometry = tuple(map(int, match.groups()))
            before = memory()
            assert 'ramfs: OK' in console.command('ramfs mount ' + mount + ' 128k')
            mounted = True
            during = memory()
            assert during['PSRAM']['free'] < before['PSRAM']['free'] - 120*1024
            monitor(80, 24)
            monitor(40, 16)
            monitor(24, 13)
            monitor(24, 10, small=True)
            if args.music:
                # Persist geometry before audio; flash writes mask the audio ISR.
                console.command('setterm size 80 24')
                console.command('lcd send ' + json.dumps('player --repeat all ' + json.dumps(args.music)))
                time.sleep(2)
                console.command('lcd key ctrlz')
                output = console.command('sessions')
                match = re.search(r'^(\d+)\s+lcd-shell\s+suspended\s+player\b', output, re.M)
                assert match, output
                player = match[1]
                monitor(80, 24, resize=False)
            if player:
                output = console.command('audio status')
                assert 'running=1 paused=0' in output and 'underruns=0' in output, output
                console.command('close ' + player)
                time.sleep(1)
                player = None
            console.command('lcd send "ltop"')
            lcd_monitor = True
            time.sleep(3)
            output = console.command('lcd dump')
            for label in ('DTCM', 'OCRAM', 'PSRAM', 'Alloc fail=', 'TASK'):
                assert label in output, output
            console.command('lcd key q')
            lcd_monitor = None
            assert 'ramfs: OK' in console.command('ramfs unmount ' + mount)
            mounted = False
            after = memory()
            assert after['PSRAM']['free'] > during['PSRAM']['free'] + 120*1024
            assert after['PSRAM']['minimum'] <= during['PSRAM']['free']
            report.update(before=before, during=during, after=after)
            report['passed'] = True
        finally:
            try:
                if active:
                    console.command('q')
                if lcd_monitor:
                    console.command('lcd key q')
                if player:
                    console.command('close ' + player)
                if mounted:
                    console.command('ramfs unmount ' + mount)
                if geometry:
                    console.command(f'setterm size {geometry[0]} {geometry[1]}')
            except Exception as error:
                report['passed'] = False
                report['cleanup_error'] = repr(error)
                raise
            finally:
                args.log.write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: RAM accounting, sampled minima, live layout and cleanup', flush=True)


if __name__ == '__main__':
    main()
