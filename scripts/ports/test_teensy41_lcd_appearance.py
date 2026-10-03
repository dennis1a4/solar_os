#!/usr/bin/env python3
"""Check LCD appearance on an idle Teensy; reboots and restores default appearance."""
import re
import time
import serial
from serial.tools import list_ports

ANSI = re.compile(r'\x1b\[[0-?]*[ -/]*[@-~]')
PROMPT = re.compile(r'[\w.-]+@[\w.-]+:/[^\n]* $')

def connect():
    end = time.monotonic() + 30
    while time.monotonic() < end:
        c = None
        ports = [p.device for p in list_ports.comports() if (p.vid, p.pid) == (0x16c0, 0x0483)]
        if len(ports) == 1:
            try:
                c = serial.Serial(ports[0], 115200, timeout=.03, write_timeout=3, exclusive=True)
                time.sleep(1)
                c.write(b'\r')
                data = b''
                ready = time.monotonic() + 2
                while time.monotonic() < ready:
                    data += c.read(16384)
                    if PROMPT.search(ANSI.sub('', data.decode(errors='replace')).replace('\r', '')):
                        return c
            except serial.SerialException:
                pass
            if c is not None:
                c.close()
        time.sleep(.2)
    raise RuntimeError('Teensy did not reconnect')

def cmd(command):
    c.write((command + '\r').encode())
    data = b''
    end = time.monotonic() + 15
    while time.monotonic() < end:
        data += c.read(16384)
        text = ANSI.sub('', data.decode(errors='replace')).replace('\r', '')
        assert 'Fault IRQ:' not in text, text
        if PROMPT.search(text):
            print(text, flush=True)
            return text
    raise AssertionError((command, data))

c = connect()
try:
    assert 'font 1x' in cmd('lcd reset')
    for scale, geometry in ((1, '100x30'), (2, '50x15'), (3, '33x10')):
        assert geometry in cmd(f'lcd font {scale}')
        assert 'Queued' in cmd('lcd send "setterm"')
        time.sleep(.3)
        dump = cmd('lcd dump')
        assert len(dump.splitlines()[1:-1]) == 30 // scale, dump
    assert 'green on blue' in cmd('lcd color green blue')
    assert 'must differ' in cmd('lcd color red red')
    assert 'usage:' in cmd('lcd font 4')
    assert 'Queued' in cmd('lcd send "calc"')
    time.sleep(.3)
    assert 'LCD is busy' in cmd('lcd font 1')
    cmd('lcd key exit')
    time.sleep(.3)
    cmd('lcd send "lcd font 2"')
    time.sleep(.3)
    assert '50x15, font 2x, color green on blue' in cmd('lcd')
    c.write(b'reboot\r')
    c.flush()
    c.close()
    time.sleep(3)
    c = connect()
    assert '50x15, font 2x, color green on blue' in cmd('lcd')
    assert 'font 1x (100x30), color white on black' in cmd('lcd reset')
    print('PASS: sizes, colors, invalid input, busy app, local command, reboot persistence, defaults')
finally:
    c.close()
