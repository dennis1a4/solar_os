#!/usr/bin/env python3
"""Exclusive USB acceptance for the native serial terminal/logger.
Transmits a short text fixture on UART8. External RX/baud/load validation is
separate; this test verifies TX logs without requiring a loopback jumper.
"""
import argparse
import json
import re
import time
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_hotplug import ANSI

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--log', type=Path, required=True)
a = parser.parse_args()
result = {'passed': False, 'commands': []}
conn = None
paths = []
session = None

def connect():
    ports = [p.device for p in list_ports.comports() if (p.vid, p.pid) == (0x16c0, 0x0483)]
    assert len(ports) == 1, ports
    return serial.Serial(ports[0], 115200, timeout=.03, write_timeout=3, exclusive=True)

def exchange(raw=b'', prompt=True, seconds=15):
    conn.write(raw)
    data = bytearray()
    start = last = time.monotonic()
    ready = False
    while time.monotonic() - start < seconds:
        chunk = conn.read(16384)
        if chunk:
            data.extend(chunk)
            last = time.monotonic()
        out = ANSI.sub('', data.decode(errors='replace')).replace('\r', '')
        ready = bool(re.search(r'[\w.-]+@[\w.-]+:/[^\n]* (?:$|\[\d+\])', out))
        if prompt and ready and time.monotonic() - last > .15:
            break
    result['commands'].append({'input': repr(raw), 'output': out})
    a.log.write_text(json.dumps(result, indent=2) + '\n')
    assert 'Fault IRQ:' not in out, out
    if prompt:
        assert ready, out[-1800:]
    return out

def cmd(text):
    return exchange((text+'\r').encode())

def memory():
    m = re.search(r'Internal heap: (\d+) free / \d+ bytes; PSRAM: (\d+) free', cmd('mem'))
    assert m
    return list(map(int, m.groups()))

try:
    conn = connect()
    time.sleep(.5)
    exchange(b'\r')
    cmd('cd /')
    assert 'uart8: closed' in cmd('serial status')
    baseline = memory()
    assert 'serial: OK' in cmd('serial config uart8 9600 7E2 xonxoff')
    assert '7E2 flow=xonxoff' in cmd('serial status')
    assert 'usage:' in cmd('serial config uart8 9600 5N1')
    assert 'serial: OK' in cmd('serial config uart8 9600 8N1 none')
    for mode, expected in [('cr', '0d'), ('lf', '0a'), ('crlf', '0d0a')]:
        path = f'/sd/serial-test-{int(time.time())}-{mode}.txt'
        paths.append(path)
        assert 'serial: OK' in cmd(f'serial record uart8 9600 {path} --timestamp')
        assert 'INVALID_STATE' in cmd('uart open uart8')
        assert 'INVALID_STATE' in cmd('serial config uart8 9600 7E1 none')
        assert 'INVALID_STATE' in cmd(f'serial record uart8 9600 {path}')
        out = exchange(f'com --hex --baud 9600 --enter {mode} uart8\r'.encode(), False, .4)
        assert 'UART8 9600' in out, out
        exchange(b'ABC\r', False, .3)
        out = exchange(b'\x1a')
        m = re.search(r'Suspended session (\d+)', out)
        assert m, out
        session = int(m[1])
        assert 'uart8: recording terminal=yes' in cmd('serial status')
        cmd(f'close {session}')
        session = None
        assert 'uart8: recording terminal=no' in cmd('serial status')
        # Recording is intentionally independent of the console connection.
        conn.close()
        time.sleep(.8)
        conn = connect()
        time.sleep(.3)
        exchange(b'\r')
        assert 'uart8: recording terminal=no' in cmd('serial status')
        assert 'serial: OK' in cmd('serial stop uart8')
        # Inspect exact transmitted data without depending on any RX wiring.
        code = f"s=open('{path}').read();print('TXCHECK', ''.join(x.split()[2] for x in s.splitlines() if ' TX ' in x))"
        assert 'TXCHECK 414243'+expected in cmd('python -c "'+code+'"')
        assert 'serial: FAIL' in cmd(f'serial record uart8 9600 {path}')
        cmd('rm '+path)
        paths.remove(path)
        assert memory() == baseline
    assert 'serial: OK' in cmd('serial config uart8 9600 7E2 none')
    out = exchange(b'com uart8\r', False, .4)
    assert '7E2 flow=none' in out
    exchange(b'\x1d')
    assert 'serial: OK' in cmd('serial config uart8 115200 8N1 none')
    # Stopping a log while COM is suspended preserves the terminal lease.
    path = f'/sd/serial-test-{int(time.time())}-raw.bin'
    paths.append(path)
    exchange(b'com uart7\r', False, .3)
    out = exchange(b'\x1a')
    session = int(re.search(r'Suspended session (\d+)', out)[1])
    assert 'serial: OK' in cmd(f'serial record uart7 115200 {path}')
    assert 'serial: OK' in cmd('serial stop uart7')
    assert 'uart7: open terminal=yes' in cmd('serial status')
    cmd(f'close {session}')
    session = None
    cmd('rm '+path)
    paths.remove(path)
    assert memory() == baseline
    result['memory'] = baseline
    result['top'] = cmd('top')
    result['passed'] = True
    print('PASS: COM baud/Enter modes, exact TX logs, suspend, disconnect, ownership, no overwrite, cleanup and memory recovery')
finally:
    if conn:
        try:
            if session:
                cmd(f'close {session}')
            cmd('serial stop uart7')
            cmd('serial stop uart8')
            for path in paths:
                cmd('rm '+path)
        except Exception as error:
            result['cleanup_error'] = str(error)
        conn.close()
    a.log.write_text(json.dumps(result, indent=2)+'\n')
