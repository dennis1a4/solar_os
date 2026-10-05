#!/usr/bin/env python3
"""Telnet hardware checks via USB control + Ethernet. Starts a password-protected
server using a unique temporary flash file, tests it, then stops/removes it.
Requires idle USB/LCD shells. Does not upload or change pin assignments.
"""
import argparse
import json
import re
import secrets
import socket
import time
import uuid
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_hotplug import Console, require, ANSI, PROMPT


class Telnet:
    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.settimeout(.15)
        self.state = 'data'
        self.option = 0
        self.sub = bytearray()
        self.closed = False

    def send(self, data):
        self.sock.sendall(data)

    def close(self):
        self.sock.close()

    def read(self, predicate, timeout=15, allow_eof=False):
        data = bytearray()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                raw = self.sock.recv(16384)
            except socket.timeout:
                continue
            except ConnectionResetError:
                raw = b''
            if not raw:
                self.closed = True
                if allow_eof:
                    return data.decode(errors='replace')
                raise RuntimeError('Telnet disconnected before expected output: ' + repr(data[-300:]))
            for b in raw:
                if self.state == 'data':
                    if b == 255:
                        self.state = 'iac'
                    elif b != 0:
                        data.append(b)
                elif self.state == 'iac':
                    if b == 255:
                        data.append(b)
                        self.state = 'data'
                    elif b in (251, 252, 253, 254):
                        self.option = b
                        self.state = 'option'
                    elif b == 250:
                        self.sub.clear()
                        self.state = 'sub'
                    else:
                        self.state = 'data'
                elif self.state == 'option':
                    if self.option == 251:
                        self.send(bytes((255, 253 if b in (1, 3) else 254, b)))
                    elif self.option == 253:
                        self.send(bytes((255, 251 if b in (3, 24, 31) else 252, b)))
                        if b == 31:
                            self.send(bytes((255, 250, 31, 0, 100, 0, 30, 255, 240)))
                    self.state = 'data'
                elif self.state == 'sub':
                    if b == 255:
                        self.state = 'subiac'
                    else:
                        self.sub.append(b)
                elif self.state == 'subiac':
                    if b == 240:
                        if self.sub == b'\x18\x01':
                            self.send(b'\xff\xfa\x18\x00xterm\xff\xf0')
                        self.state = 'data'
                    else:
                        self.sub.append(b)
                        self.state = 'sub'
            out = ANSI.sub('', data.decode(errors='replace')).replace('\r', '')
            if predicate(out):
                return out
        raise TimeoutError('Telnet expected output missing: ' + repr(data[-500:]))

    def command(self, command):
        self.send(command.encode() + b'\r\n')
        return self.read(lambda s: bool(PROMPT.search(s)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--port', type=int, default=23)
    args = parser.parse_args()
    secret = secrets.token_hex(16)
    fixture = '/flash/_telnet_' + uuid.uuid4().hex[:8] + '.pass'
    report = dict(passed=False, commands=[], checks=[], memory={}, fixture=fixture)
    usb = None
    remote = None
    c = None
    created = False

    def passed(name):
        report['checks'].append(name)
        print('PASS:', name, flush=True)

    def memory(name):
        out = c.command('mem')
        m = re.search(r'Internal heap: (\d+) free / (\d+) bytes; PSRAM: (\d+) free / (\d+)', out)
        require(m is not None, out)
        report['memory'][name] = list(map(int, m.groups()))
        regions = re.search(r'OCRAM: (\d+) free / (\d+) bytes', out)
        require(regions is not None, out)
        report.setdefault('ocram', {})[name] = list(map(int, regions.groups()))

    def get_ip():
        out = c.poll('network status', lambda s: 'link=up' in s and 'address=0.0.0.0' not in s, timeout=25)
        return re.search(r'address=(\d+\.\d+\.\d+\.\d+)', out).group(1)

    def login():
        t = Telnet(host, args.port)
        prompt = t.read(lambda s: 'Password: ' in s)
        require('heap:' not in prompt, 'Unauthenticated shell output')
        # Deliberately split the password to exercise streaming input.
        t.send(secret[:7].encode())
        time.sleep(.05)
        t.send(secret[7:].encode() + b'\r\n')
        out = t.read(lambda s: bool(PROMPT.search(s)))
        require(secret not in out, 'Password echoed')
        return t

    try:
        ports = [p.device for p in list_ports.comports() if (p.vid, p.pid) == (0x16c0, 0x0483)]
        require(len(ports) == 1, 'Expected one Teensy serial port')
        usb = serial.Serial(ports[0], 115200, timeout=.05, write_timeout=3, exclusive=True)
        time.sleep(.5)
        usb.reset_input_buffer()
        c = Console(usb, report)
        c.command('')
        require('stopped' in c.command('telnetd status'), 'An existing server must not be replaced by this test')
        c.command('network up')
        host = get_ip()
        report['ip'] = host
        memory('before')
        usb_pwd = c.command('pwd')
        c.command('python', python=True)
        c.py("f=open('" + fixture + "','x'); f.write('" + secret + "\\n'); f.close()")
        created = True
        usb.write(b'\x04')
        c.command('')
        c.poll('telnetd status', lambda s: 'console stack: 0 bytes' in s)
        memory('stopped_baseline')
        require('listening' in c.command(f'telnetd start {fixture} {args.port}'), 'Server failed to start')
        memory('listening')
        require(report['ocram']['stopped_baseline'][0] - report['ocram']['listening'][0] >= 40960,
                'Telnet stack was not allocated from OCRAM')
        remote = Telnet(host, args.port)
        remote.read(lambda s: 'Password: ' in s)
        remote.send(b'wrong-password\r\n')
        out = remote.read(lambda s: False, allow_eof=True)
        require('Authentication failed' in out, 'Wrong password not rejected')
        remote.close()
        passed('wrong password rejected')
        remote = login()
        require('size 100 30' in remote.command('setterm'), 'NAWS not applied')
        remote.send(bytes((255, 250, 31, 0, 80, 0, 24, 255, 240)))
        require('size 80 24' in remote.command('setterm'), 'Live NAWS resize not applied')
        require('telnetd' in remote.command('commands'), 'Remote command list unavailable')
        remote.send(b'watch -n 1 uptime\r\n')
        remote.read(lambda s: len(set(re.findall(r'Uptime=(\d+)', s))) >= 2)
        remote.send(b'q')
        require('watch stopped' in remote.read(lambda s: bool(PROMPT.search(s))), 'Remote watch did not stop')
        remote.send(b'man watch\r\n')
        remote.read(lambda s: 'man command.watch' in s)
        remote.send(b'q')
        remote.read(lambda s: bool(PROMPT.search(s)))
        require('telnet-shell' in remote.command('session list'), 'Remote session listing missing')
        passed('remote watch ticks/cancellation, manual pager and session listing')
        remote.send(b'calc\r\n')
        remote.read(lambda s: s.endswith('> '))
        remote.send(b'456\x1a')
        suspended = remote.read(lambda s: bool(PROMPT.search(s)))
        match = re.search(r'Suspended session (\d+)', suspended)
        require(match is not None, 'Remote calculator did not suspend')
        remote_id = match.group(1)
        require('suspended' in remote.command('sessions'), 'Remote retained session missing')
        remote.send(b'fg\r\n')
        remote.read(lambda s: 'calculator resumed' in s and '456' in s)
        remote.send(b'\r\n')
        remote.read(lambda s: '456' in s)
        remote.send(b'\x1d')
        remote.read(lambda s: bool(PROMPT.search(s)))
        require('no such app' in remote.command('fg ' + remote_id), 'Finished ID remained valid')
        passed('remote Ctrl+Z/fg preserves calculator input and releases completed ID')
        require('clock' in remote.command('apps'), 'Remote apps unavailable')
        require('display-only' in remote.command('clock'), 'Graphical app not rejected')
        remote.command('cd /flash')
        require('/flash' in remote.command('pwd'), 'Remote cwd not changed')
        require(c.command('pwd') == usb_pwd, 'USB cwd changed with Telnet')
        busy = socket.create_connection((host, args.port), timeout=5)
        busy.settimeout(5)
        require(b'busy' in busy.recv(1024), 'Second client not rejected')
        busy.close()
        passed('login, NAWS, shell, graphics rejection, cwd isolation, busy rejection')
        remote.send(b'calc\r\n')
        remote.read(lambda s: s.endswith('> '))
        remote.send(b'\x1a')
        remote.read(lambda s: bool(PROMPT.search(s)))
        remote.send(b'files /flash\r\n')
        remote.read(lambda s: 'F3 ' in s and 'F9 ' in s)
        remote.close()
        c.poll('telnetd status', lambda s: 'client=none' in s)
        remote = login()
        require(':/ ' in remote.command('pwd'), 'New client inherited cwd')
        remote.send(b'files /flash\r\n')
        remote.read(lambda s: 'F3 ' in s and 'F9 ' in s)
        remote.send(b'\x1d')
        remote.read(lambda s: bool(PROMPT.search(s)))
        require(not re.search(r'^\d+\s+telnet-shell\s+suspended', c.command('sessions'), re.M),
                'Disconnected Telnet retained an app')
        remote.send(b'calc\r\n')
        remote.read(lambda s: s.endswith('> '))
        remote.send(b'\x1d')
        remote.read(lambda s: bool(PROMPT.search(s)))
        passed('disconnect releases active Files and retained Calc; reconnect gets fresh shell')
        remote.send(b'python\r\n')
        remote.read(lambda s: s.endswith('>>> '))
        remote.send(b"exec('while True: pass')\r\n")
        time.sleep(.5)
        remote.close()
        c.poll('telnetd status', lambda s: 'client=none' in s)
        remote = login()
        require('alive' in remote.command('echo alive'), 'Interpreter disconnect did not recover')
        passed('disconnect cancels running Python and frees app ownership')
        remote.send(b'exit\r\n')
        remote.read(lambda s: False, allow_eof=True)
        remote.close()
        passed('exit closes only remote session')
        time.sleep(1)
        memory('warm_idle')
        for i in range(10):
            remote = login()
            require('cycle-ok' in remote.command('echo cycle-ok'), 'Reconnect failed')
            remote.close()
            c.poll('telnetd status', lambda s: 'client=none' in s)
        time.sleep(1)
        memory('after_10_cycles')
        require(report['memory']['after_10_cycles'][2] == report['memory']['warm_idle'][2],
                'PSRAM not recovered after reconnect cycles')
        passed('ten authenticated reconnect cycles')
        remote = login()
        c.command('network down')
        remote.read(lambda s: False, timeout=5, allow_eof=True)
        remote.close()
        c.command('network up')
        host = get_ip()
        remote = login()
        require('restored' in remote.command('echo restored'), 'Network restart recovery failed')
        passed('network down/up restores listener with fresh authentication')
        c.command('telnetd stop')
        remote.read(lambda s: False, allow_eof=True)
        remote.close()
        require('stopped' in c.command('telnetd status'), 'Server stop failed')
        passed('stop disconnects client and closes listener')
        for i in range(3):
            require('listening' in c.command(f'telnetd start {fixture} {args.port}'), 'Immediate restart failed')
            remote = login()
            c.command('telnetd stop')
            remote.read(lambda s: False, allow_eof=True)
            remote.close()
        passed('three immediate stop/start cycles with real clients')
        time.sleep(1)
        c.poll('telnetd status', lambda s: 'console stack: 0 bytes' in s)
        memory('stopped')
        require(report['ocram']['stopped'][0] == report['ocram']['stopped_baseline'][0],
                'Telnet/Python stack memory did not return after stop')
        # Self-stop must unwind before a local console can delete its stack.
        require('listening' in c.command(f'telnetd start {fixture} {args.port}'), 'Self-stop setup failed')
        remote = login(); remote.send(b'telnetd stop\r\n')
        remote.read(lambda s: False, allow_eof=True); remote.close()
        c.poll('telnetd status', lambda s: 'console stack: 0 bytes' in s)
        memory('self_stopped')
        require(report['ocram']['self_stopped'][0] == report['ocram']['stopped_baseline'][0],
                'Self-stop leaked its stack')
        # Stop the daemon with a running Python app; both stacks must unwind.
        require('listening' in c.command(f'telnetd start {fixture} {args.port}'), 'Python-stop setup failed')
        remote = login(); remote.send(b'python\r\n')
        remote.read(lambda s: s.endswith('>>> '))
        remote.send(b"exec('while True: pass')\r\n"); time.sleep(.5)
        c.command('telnetd stop'); remote.close()
        c.poll('telnetd status', lambda s: 'console stack: 0 bytes' in s)
        time.sleep(1); memory('python_stopped')
        require(report['ocram']['python_stopped'][0] == report['ocram']['stopped_baseline'][0],
                'Stopping remote Python did not recover both stacks')
        passed('on-demand OCRAM stack recovery, self-stop and stop during Python')
        require('/sd mounted' in c.command('sd status'), 'SD unavailable')
        require('/usb,' in c.command('usb status'), 'USB drive unavailable')
        require('keyboard=connected' in c.command('lcd'), 'Keyboard absent')
        report['passed'] = True
    except BaseException as exc:
        report['error'] = repr(exc).replace(secret, '<redacted>')
        raise
    finally:
        if remote:
            remote.close()
        if c:
            try:
                if c.python:
                    usb.write(b'\x04')
                    c.command('')
                if created:
                    c.command('telnetd stop')
                    c.command('rm ' + fixture)
            except Exception as exc:
                report['cleanup_error'] = repr(exc).replace(secret, '<redacted>')
                report['passed'] = False
        if usb:
            usb.close()
        args.log.write_text(json.dumps(report, indent=2).replace(secret, '<redacted>') + '\n')
        print('PASS' if report['passed'] else 'FAIL', args.log, flush=True)


if __name__ == '__main__':
    main()
