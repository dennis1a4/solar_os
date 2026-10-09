#!/usr/bin/env python3
"""Live small-screen dashboard timing and shared-SPI regression check."""
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
    c = Console(usb, report)
    graphics = False
    def status():
        out = c.command('lcd small')
        assert 'small=on' in out, out
        assert 'RAM used/total KiB' in out and 'Disk used/total MiB' in out, out
        for label in ('DTCM', 'OCRAM', 'PSRAM', 'SD', 'USB', 'Flash'):
            assert label in out, out
        return {key: int(value) for key, value in re.findall(r'([\w-]+)=(\d+)', out)}
    try:
        c.command('')
        c.command('lcd small on')
        time.sleep(2)
        first = status()
        started = time.monotonic()
        # Includes at least one slow storage refresh, plus USB traffic.
        for _ in range(7):
            time.sleep(5)
            assert 'DASHBOARD_ALIVE' in c.command('echo DASHBOARD_ALIVE')
        end = status()
        elapsed = time.monotonic() - started
        report['monitor_cpu_percent'] = ((end['runtime-us'] - first['runtime-us']) & 0xffffffff) / (elapsed * 10000)
        assert report['monitor_cpu_percent'] < 2, report['monitor_cpu_percent']
        assert end['updates'] >= first['updates'] + 30, (first, end)
        assert end['stack-free'] >= 512, end
        assert end['spi-misses'] == first['spi-misses'], (first, end)
        c.command('lcd send "invaders"')
        graphics = True
        time.sleep(2)
        before = status()
        frames = int(re.search(r'frames=(\d+)', c.command('lcd'))[1])
        time.sleep(5)
        after = status()
        assert after['updates'] > before['updates'], (before, after)
        assert int(re.search(r'frames=(\d+)', c.command('lcd'))[1]) > frames
        c.command('lcd key exit')
        graphics = False
        c.command('lcd small off')
        time.sleep(2)
        paused = c.command('lcd small')
        time.sleep(2)
        paused2 = c.command('lcd small')
        assert 'small=off' in paused2
        assert re.search(r'updates=(\d+)', paused)[1] == re.search(r'updates=(\d+)', paused2)[1]
        c.command('lcd small on')
        time.sleep(2)
        status()
        disk = c.command('df')
        memory = c.command('mem')
        display = c.command('lcd small')
        for label in ('DTCM', 'OCRAM', 'PSRAM'):
            match = re.search(label + r' .*?used=(\d+) free=\d+ capacity=(\d+)', memory)
            assert match, memory
            used, total = map(int, match.groups())
            assert re.search(label + r'\s+' + str(used//1024) + '/' + str(total//1024), display), display
        for mount, label in (('/sd', 'SD'), ('/flash', 'Flash')):
            match = re.search(re.escape(mount) + r'\s+(\d+)\s+(\d+)\s+\d+', disk)
            if match:
                total, used = map(int, match.groups())
                assert re.search(label + r'\s+' + str(used//1024) + '/' + str(total//1024), display), display
        report['passed'] = True
    finally:
        try:
            if graphics:
                c.command('lcd key exit')
            c.command('lcd small on')
        finally:
            args.log.write_text(json.dumps(report, indent=2) + '\n')
print('PASS: dashboard refresh, storage, graphics coexistence, pause/resume; CPU %.3f%%' % report['monitor_cpu_percent'])
