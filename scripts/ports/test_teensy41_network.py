#!/usr/bin/env python3
"""Check native Ethernet DHCP and optional DNS/TCP through the USB shell.
Requires the PJRC kit connected to a DHCP LAN; does not scan the LAN.
"""
import argparse
import json
import re
import time
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_shell import ANSI, PROMPT

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--log', type=Path, required=True)
p.add_argument('--resolve', help='Optional single hostname to resolve')
p.add_argument('--connect', nargs=2, metavar=('HOST', 'PORT'), help='Optional single TCP endpoint to check')
a = p.parse_args()
result = {'passed': False, 'commands': []}
try:
    ports = [p.device for p in list_ports.comports() if (p.vid, p.pid) == (0x16c0, 0x0483)]
    assert len(ports) == 1, ports
    with serial.Serial(ports[0], 115200, timeout=.02, write_timeout=3, exclusive=True) as conn:
        time.sleep(1)
        conn.reset_input_buffer()
        def command(s):
            assert '\n' not in s and '\r' not in s
            conn.write((s+'\r').encode())
            data = bytearray()
            deadline = time.monotonic()+15
            while time.monotonic() < deadline:
                data.extend(conn.read(8192))
                text = ANSI.sub('', data.decode(errors='replace')).replace('\r', '')
                if text.endswith(PROMPT):
                    result['commands'].append({'input': s, 'output': text})
                    return text
            raise RuntimeError(f'No prompt after {s}: {data[-1000:]!r}')
        command('')
        assert 'eth0:' in command('network status')
        assert 'started' in command('network up')
        deadline = time.monotonic()+30
        while True:
            status = command('network status')
            if 'link=up' in status and 'DHCP=bound' in status:
                break
            assert time.monotonic() < deadline, status
            time.sleep(1)
        result['status'] = status
        if a.resolve:
            response = command('network resolve '+a.resolve)
            assert re.search(r'^\d{1,3}(?:\.\d{1,3}){3}$', response, re.M), response
        if a.connect:
            assert ' connected\n' in command('network connect '+' '.join(a.connect))
        assert 'usage:' in command('network connect localhost 70000')
        assert 'stopped' in command('network down')
        assert 'started' in command('network up')
        deadline = time.monotonic()+30
        while True:
            status = command('network status')
            if 'link=up' in status and 'DHCP=bound' in status:
                break
            assert time.monotonic() < deadline, status
            time.sleep(1)
        result['memory'] = command('mem')
        result['uptime'] = command('uptime')
        result['passed'] = True
        print('PASS: Ethernet link, DHCP, restart and requested DNS/TCP checks')
finally:
    a.log.write_text(json.dumps(result, indent=2)+'\n')
