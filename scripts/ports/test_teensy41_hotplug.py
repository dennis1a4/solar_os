#!/usr/bin/env python3
"""Guided SD/USB/keyboard recovery tests. Requires both consoles at idle shells.
Writes only a unique fixture directory; leaves it for inspection. Default mode
checks software eject/remount. --physical requires a local operator and tests
read-only surprise removal. Never formats, reboots, uploads or simulates unplug.
"""
import argparse
import json
import re
import sys
import time
import uuid
from pathlib import Path

ANSI = re.compile(r'\x1b\[[0-?]*[ -/]*[@-~]')
PROMPT = re.compile(r'[\w.-]+@[\w.-]+:/[^\n]* $')


def require(condition, detail):
    if not condition:
        raise AssertionError(detail)


def mounted(device, output):
    return ('SD: /sd mounted' if device == 'sd' else 'USB: /usb,') in output


class Console:
    def __init__(self, port, report):
        self.port, self.report = port, report
        self.python = False

    def command(self, command, python=False):
        self.port.write((command + '\r').encode())
        data = b''
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            data += self.port.read(16384)
            output = ANSI.sub('', data.decode(errors='replace')).replace('\r', '')
            if (output.endswith('>>> ') if python else PROMPT.search(output)):
                self.report['commands'].append(dict(command=command, output=output))
                self.python = python
                return output
        self.report['commands'].append(dict(command=command, output=data.decode(errors='replace'), timeout=True))
        raise TimeoutError(command)

    def py(self, command):
        output = self.command(command, python=True)
        require('Traceback' not in output, output)
        return output

    def poll(self, command, predicate, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            output = self.command(command)
            if predicate(output):
                return output
            time.sleep(.25)
        raise AssertionError(('condition timed out', command, output))

    def lcd(self, command):
        require('"' not in command and len(command) <= 200, command)
        return self.command('lcd send "' + command.replace('\\', '\\\\') + '"')

    def marker(self, token):
        return self.poll('lcd dump', lambda out: token in [line.strip() for line in out.splitlines()])


def operator(report, instruction):
    reply = input(instruction + '\nType done when complete (anything else aborts): ').strip().lower()
    report['operator'].append(dict(instruction=instruction, reply=reply))
    require(reply == 'done', 'Operator aborted')


def storage(c, args, report):
    device = args.device
    status = lambda: c.command(device + ' status')
    require(mounted(device, status()), 'Start with the test medium mounted')
    require('0 open handles' in status(), 'Close all apps/files on this medium first')
    root = '/' + device + '/_hotplug_' + uuid.uuid4().hex[:8]
    report['fixture'] = root
    require('mkdir:' not in c.command('mkdir ' + root), root)
    c.command('python', python=True)
    c.py("f=open('%s/data.bin','wb'); assert f.write(bytes(range(256))*32)==8192; f.close()" % root)
    c.command('\x04')

    def verify():
        c.command('python', python=True)
        c.py("f=open('%s/data.bin','rb'); assert f.read()==bytes(range(256))*32; f.close()" % root)
        c.command('\x04')

    verify()
    for cycle in range(args.cycles):
        require('safe to' in c.command(device + ' eject'), 'Eject failed')
        require(not mounted(device, status()), 'Eject did not unmount')
        if args.physical:
            operator(report, f'Cycle {cycle + 1}: remove only the {device} medium; leave it out.')
            c.command('ls /flash')
            operator(report, f'Reinsert the same {device} medium.')
            c.poll(device + ' status', lambda out: mounted(device, out))
        else:
            require(mounted(device, c.command(device + ' mount')), 'Remount failed')
        verify()
        report['completed_cycles'] = cycle + 1
        report.setdefault('memory', []).append(c.command('mem'))

    # Hold a file on the independent LCD interpreter, leaving USB shell usable.
    lcd_started = False
    try:
        lcd_started = True
        c.lcd('python')
        c.poll('lcd dump', lambda out: any(line.strip().endswith('>>>') for line in out.splitlines()))
        c.lcd("f=open('%s/data.bin','rb')" % root)
        c.poll(device + ' status', lambda out: '1 open handles' in out)
        require('busy' in c.command(device + ' eject'), 'Eject accepted an open handle')
        if args.physical:
            operator(report, f'Remove only {device}, with the read-only test handle still open.')
            c.poll(device + ' status', lambda out: not mounted(device, out))
            c.command('ls /flash')
            # Print an exact success line only for ENODEV, not a cached read.
            token = str(int(uuid.uuid4().hex[:7], 16))
            c.lcd("exec('try:\\n f.read(1)\\nexcept OSError as e:\\n print(%s if e.args[0]==19 else e)')" % token)
            c.marker(token)
            operator(report, f'Reinsert the same {device} medium before the old handle is closed.')
            time.sleep(3)
            require(not mounted(device, status()), 'Remounted with a stale handle')
            require('stale' in c.command(device + ' mount'), 'Manual mount bypassed stale handles')
        c.command('lcd key exit')
        lcd_started = False
        c.poll(device + ' status', lambda out: mounted(device, out) and '0 open handles' in out)
        verify()
    finally:
        if lcd_started:
            c.command('lcd key exit')
    report['busy_handle_check'] = True
    report['stale_handle_physical_check'] = args.physical


def keyboard(c, args, report):
    require(args.physical, 'Keyboard reconnect requires --physical')
    require('keyboard=connected' in c.command('lcd'), 'Start with keyboard connected')
    usb_present = mounted('usb', c.command('usb status'))
    for cycle in range(args.cycles):
        modifier = ('no key', 'Shift', 'Ctrl', 'Right arrow')[cycle % 4]
        operator(report, f'Cycle {cycle + 1}: hold {modifier}, unplug only the keyboard, then release the key. Leave keyboard out.')
        c.poll('lcd', lambda out: 'keyboard=absent' in out)
        operator(report, 'Reconnect the keyboard.')
        c.poll('lcd', lambda out: 'keyboard=connected' in out)
        token = 'Reconnect_' + uuid.uuid4().hex[:8]
        operator(report, f'Using the physical keyboard, clear any partial line and type: echo {token}\nPress Enter. Check arrows and Backspace too.')
        c.marker(token)
        if usb_present:
            require(mounted('usb', c.command('usb status')), 'Keyboard reconnect lost USB mount')
            require('ls:' not in c.command('ls /usb'), 'USB unreadable after keyboard reconnect')
        report['completed_cycles'] = cycle + 1
        report.setdefault('memory', []).append(c.command('mem'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', choices=('sd', 'usb', 'keyboard'), required=True)
    parser.add_argument('--physical', action='store_true')
    parser.add_argument('--cycles', type=int, default=3)
    parser.add_argument('--port')
    parser.add_argument('--log', type=Path, required=True)
    args = parser.parse_args()
    if args.cycles < 1:
        parser.error('--cycles must be positive')
    if args.device == 'keyboard' and not args.physical:
        parser.error('keyboard requires --physical')
    if args.physical and not sys.stdin.isatty():
        parser.error('--physical requires an interactive local operator')
    # Defer optional dependency so --help and host protocol tests work anywhere.
    import serial
    from serial.tools import list_ports
    report = dict(passed=False, device=args.device, physical=args.physical, commands=[], operator=[])
    try:
        ports = [args.port] if args.port else [p.device for p in list_ports.comports() if (p.vid, p.pid) == (0x16c0, 0x0483)]
        require(len(ports) == 1, ('Specify --port', ports))
        with serial.Serial(ports[0], 115200, timeout=.05, write_timeout=3, exclusive=True) as port:
            time.sleep(.5)
            port.reset_input_buffer()
            c = Console(port, report)
            c.command('')
            c.command('cd /')
            try:
                (keyboard if args.device == 'keyboard' else storage)(c, args, report)
            finally:
                if c.python:
                    c.command('\x04')
            report['passed'] = True
    except BaseException as exc:
        report['error'] = repr(exc)
        raise
    finally:
        args.log.write_text(json.dumps(report, indent=2) + '\n')
        print('PASS' if report['passed'] else 'FAIL', args.log)


if __name__ == '__main__':
    main()
