#!/usr/bin/env python3
"""Bounded shell composition acceptance on an idle legacy-wiring Teensy.

Uses unique SD fixtures and RAMFS mounts, removed at completion. Does not change
RTC or preferences. Temporarily starts Ethernet for a local UDP fixture.
Requires exclusive USB and an idle LCD shell.
"""
import argparse
import json
import re
import socket
import struct
import threading
import time
import uuid
from pathlib import Path

import serial
from serial.tools import list_ports
from test_teensy41_hotplug import ANSI


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', type=Path, required=True)
    args = parser.parse_args()
    report = {'passed': False, 'commands': []}
    ports = [p.device for p in list_ports.comports() if (p.vid, p.pid) == (0x16c0, 0x0483)]
    assert len(ports) == 1, ports
    conn = serial.Serial(ports[0], 115200, timeout=.03, write_timeout=3, exclusive=True)
    root = '/sd/_sc' + uuid.uuid4().hex[:6]
    mounts = []
    created = False
    udp = None
    network_was_up = None
    udp_stop = threading.Event()
    udp_mode = ["valid"]

    def exchange(raw, prompt=True, timeout=20):
        conn.write(raw)
        data = bytearray()
        start = last = time.monotonic()
        out = ''
        ready = False
        while time.monotonic() - start < timeout:
            chunk = conn.read(16384)
            if chunk:
                data.extend(chunk)
                last = time.monotonic()
            out = ANSI.sub('', data.decode(errors='replace')).replace('\r', '')
            ready = bool(re.search(r'[\w.-]+@[\w.-]+:/[^\n]* (?:$|\[\d+\])', out))
            if prompt and ready and time.monotonic() - last > .1:
                break
        report['commands'].append({'input': repr(raw), 'output': out})
        args.log.write_text(json.dumps(report, indent=2) + '\n')
        assert 'Fault IRQ:' not in out, out
        if prompt:
            assert ready, out[-2000:]
        return out

    def cmd(text):
        assert len(text.encode()) <= 191, text
        return exchange((text + '\r').encode())

    def line(out, text):
        return text in out.splitlines()

    def py(code):
        out = cmd('python -c ' + json.dumps(code))
        assert 'Traceback' not in out, out
        return out

    def mem():
        out = cmd('mem')
        match = re.search(r'Internal heap: (\d+) free / \d+ bytes; PSRAM: (\d+) free', out)
        assert match, out
        return list(map(int, match.groups()))

    try:
        time.sleep(.5)
        cmd('')
        assert not re.search(r'script[0-3] (?:running|waiting|queued)', cmd('jobs'))
        state = cmd('sessions')
        assert not re.search(r' lcd-shell\s+(?:foreground|suspended)', state), state
        cmd('mkdir ' + root)
        created = True
        for name, expression in [('lines', "b'one\\ntwo words\\nlast'"), ('exact', "b'x'*8192"),
                                 ('big', "b'x'*8193"), ('binary', "b'a\\x00b\\n'")]:
            py("f=open(%r,'wb');f.write(%s);f.close()" % (root + '/' + name, expression))
        out = cmd('date; time')
        assert re.search(r'\n20\d\d-\d\d-\d\d\n\d\d:\d\d:\d\d\n', out), out
        assert line(cmd('echo "a;b&&c|d";echo DONE'), 'a;b&&c|d')
        out = cmd('cd ' + root + '/absent&&echo WRONG;echo RIGHT')
        assert not line(out, 'WRONG') and line(out, 'RIGHT'), out
        assert line(cmd('echo OK&&echo SECOND'), 'SECOND')
        assert line(cmd('cat ' + root + '/lines|grep words|wc -w'), '2')
        assert line(cmd('cat ' + root + '/lines|head -n 1|cat'), 'one')
        assert line(cmd('cat ' + root + '/lines|grep absent|wc -c'), '0')
        assert line(cmd('cat ' + root + '/exact|wc -c'), '8192')
        assert line(cmd('cat ' + root + '/binary|wc -c'), '4')
        out = cmd('cat ' + root + '/big|wc -c&&echo WRONG;echo RECOVERED')
        assert 'exceeds 8192' in out and not line(out, 'WRONG') and line(out, 'RECOVERED'), out
        assert 'not supported in a chain' in cmd('echo WRONG;clock')
        assert line(cmd('echo x|head -n 0|wc -c'), '0')
        # Warm the paths before checking exact resource recovery.
        cmd('ls ' + root + '|grep lines|wc -l')
        baseline = mem()
        report['memory_before'] = baseline
        for _ in range(20):
            assert line(cmd('cat ' + root + '/lines|grep words|wc -l'), '1')
            cmd('cat ' + root + '/big|wc -c')
            cmd('date;time')
        report['memory_after'] = mem()
        assert report['memory_after'] == baseline, report
        # Keep most PSRAM occupied while exercising the same bounded pipeline.
        for suffix, size in [('a', '4m'), ('b', '3m')]:
            name = '/sc' + uuid.uuid4().hex[:5] + suffix
            out = cmd('ramfs mount ' + name + ' ' + size)
            assert 'ramfs: OK' in out, out
            mounts.append(name)
        report['memory_under_pressure'] = mem()
        assert line(cmd('cat ' + root + '/lines|grep words|wc -l'), '1')
        for name in mounts[:]:
            assert 'ramfs: OK' in cmd('ramfs unmount ' + name)
            mounts.remove(name)
        assert mem() == baseline
        # Independent LCD shell uses the same composition path and separate I/O.
        cmd('lcd send ' + json.dumps('echo LCD_PIPE | wc -c'))
        time.sleep(.3)
        assert re.search(r'\b9\b', cmd('lcd dump'))
        # Local NTP fixture verifies real status propagation without setting RTC.
        before_network = cmd('network status')
        network_was_up = 'eth0: stopped' not in before_network
        if not network_was_up:
            cmd('network up')
        deadline = time.monotonic() + 30
        while True:
            state = cmd('network status')
            match = re.search(r'address=(\d+\.\d+\.\d+\.\d+)', state)
            if match and match[1] != '0.0.0.0' and 'link=up' in state:
                break
            assert time.monotonic() < deadline, state
            time.sleep(.5)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as route:
            route.connect((match[1], 9))
            host = route.getsockname()[0]
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp.bind((host, 0))
        udp.settimeout(.2)
        target = '%s %d' % udp.getsockname()
        def serve():
            while not udp_stop.is_set():
                try:
                    request, peer = udp.recvfrom(512)
                except socket.timeout:
                    continue
                except OSError:
                    break
                if len(request) < 48 or udp_mode[0] == 'drop':
                    continue
                reply = bytearray(48)
                reply[0] = 0x24
                reply[1] = 0 if udp_mode[0] == 'denied' else 2
                reply[24:32] = request[40:48]
                for offset in (32, 40):
                    struct.pack_into('!II', reply, offset, int(time.time()) + 2208988800, 0)
                udp.sendto(reply, peer)
        threading.Thread(target=serve, daemon=True).start()
        out = cmd('ntp -q ' + target + '&&echo NTP_OK')
        assert line(out, 'NTP_OK'), out
        udp_mode[0] = 'denied'
        out = cmd('ntp -q ' + target + '&&echo WRONG;echo DENIED_OK')
        assert not line(out, 'WRONG') and line(out, 'DENIED_OK'), out
        udp_mode[0] = 'drop'
        out = exchange(('ntp -q ' + target + ';echo CANCEL_WRONG\r').encode(), False, .5)
        assert not line(out, 'CANCEL_WRONG'), out
        out = exchange(b'\x03')
        assert 'stopped' in out and not line(out, 'CANCEL_WRONG'), out
        report['tasks'] = cmd('top')
        report['passed'] = True
        print('PASS: USB/LCD chains, failure short-circuiting, binary/exact/overflow pipes, '
              '20-cycle memory recovery, 7 MiB RAMFS pressure and cancellation')
    finally:
        try:
            udp_stop.set()
            if udp is not None:
                udp.close()
            if network_was_up is False:
                cmd('network down')
            for name in mounts:
                cmd('ramfs unmount ' + name)
            if created:
                cmd('rm -rf ' + root)
        finally:
            conn.close()
            args.log.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
