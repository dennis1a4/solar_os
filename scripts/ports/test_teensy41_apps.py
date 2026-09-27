#!/usr/bin/env python3
"""Hardware tests for saved settings, less, Notes and Sheet (pyserial + pyte).
Retains unique SD/flash fixtures. Restores identity, terminal size, and startup
selection; never overwrites an existing startup script.
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
from test_teensy41_shell import ANSI

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--log', type=Path, required=True)
parser.add_argument('--restore-from', type=Path, help='Restore preferences from an interrupted test log first')
args = parser.parse_args()
name = '_apps_' + uuid.uuid4().hex[:8]
report = {'passed': False, 'fixture': name, 'commands': []}
screen = pyte.Screen(80, 24)
stream = pyte.Stream(screen)
prompt = re.compile(r'[\w.-]+@[\w.-]+:/[^\n]* $')
conn = None
original = None
startup_created = False


def connect():
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        ports = [p.device for p in list_ports.comports() if (p.vid, p.pid) == (0x16c0, 0x0483)]
        if len(ports) == 1:
            try:
                return serial.Serial(ports[0], 115200, timeout=.02, write_timeout=3, exclusive=True)
            except (OSError, serial.SerialException):
                pass
        time.sleep(.2)
    raise RuntimeError('Teensy serial device unavailable')


def exchange(raw=b'', *, shell=False, suffix=None, expect=None, timeout=20):
    conn.write(raw)
    data = bytearray()
    last = time.monotonic()
    deadline = last + timeout
    while time.monotonic() < deadline:
        chunk = conn.read(8192)
        if chunk:
            data.extend(chunk)
            stream.feed(chunk.decode(errors='replace'))
            last = time.monotonic()
        text = ANSI.sub('', data.decode(errors='replace')).replace('\r', '')
        display = '\n'.join(screen.display)
        assert 'Fault IRQ:' not in text, text
        ready = bool(prompt.search(text)) if shell else text.endswith(suffix) if suffix else time.monotonic()-last > .25
        if ready and (expect is None or expect in display or expect in text):
            report['commands'].append({'input': repr(raw), 'output': text, 'screen': display})
            args.log.write_text(json.dumps(report, indent=2) + '\n')
            return text
    raise RuntimeError(f'Timeout: {raw!r}\n{data[-1800:]!r}\n{display}')


def cmd(text):
    return exchange((text+'\r').encode(), shell=True)


def py(text):
    result = exchange((text+'\r').encode(), suffix='>>> ')
    assert 'Traceback' not in result, result
    return result


def reboot():
    global conn
    # Keep DTR asserted until the console has consumed the command. Closing
    # immediately can defer the reboot until the next serial connection opens.
    exchange(b'reboot\r', suffix='rebooting\n')
    conn.close()
    time.sleep(5)
    conn = connect()
    time.sleep(1)
    return exchange(shell=True, timeout=30)


def memory():
    text = cmd('mem')
    match = re.search(r'Internal heap: (\d+) free / \d+ bytes; PSRAM: (\d+) free', text)
    assert match, text
    return tuple(map(int, match.groups()))


try:
    conn = connect()
    time.sleep(1)
    exchange(b'\x1d\r', shell=True)
    cmd('cd /')
    if args.restore_from:
        saved = json.loads(args.restore_from.read_text())['original_settings']
        cmd('identity user '+saved[0])
        cmd('identity hostname '+saved[1])
        cmd('setterm size '+saved[2]+' '+saved[3])
        cmd('setterm startup '+saved[4])
    identity = cmd('identity')
    match = re.search(r'\n([\w.-]+)@([\w.-]+)\n', identity)
    assert match, identity
    settings = cmd('setterm')
    geometry = re.search(r'size (\d+) (\d+); startup (\w+)', settings)
    assert geometry, settings
    original = (*match.groups(), *geometry.groups())
    report['original_settings'] = original
    screen.resize(int(original[3]), int(original[2]))
    apps = cmd('apps')
    for app in ('less', 'notes', 'sheet', 'files', 'ssh'):
        assert app+' - ' in apps, apps
    assert 'INVALID_ARG' in cmd('identity user bad/name')
    assert 'usage:' in cmd('setterm size 1 1')
    assert 'INVALID_ARG' in cmd('setterm startup bad')
    assert 'saved' in cmd('identity user settingstest')
    assert 'saved' in cmd('identity hostname keyboardtest')
    cmd('setterm size 100 30')
    screen.resize(30, 100)
    assert 'saved' in cmd('setterm startup flash')
    boot = reboot()
    assert 'settingstest@keyboardtest:/' in boot, boot
    assert 'size 100 30; startup flash' in cmd('setterm')
    # Restore identity/geometry early, so existing regression clients still work.
    assert 'saved' in cmd('identity user '+original[0])
    assert 'saved' in cmd('identity hostname '+original[1])
    cmd('setterm size 80 24')
    screen.resize(24, 80)
    cmd('setterm startup '+original[4])
    reboot()
    assert original[0]+'@'+original[1] in cmd('identity')
    assert 'size 80 24; startup '+original[4] in cmd('setterm')
    for volume in ('/sd', '/flash'):
        root = volume+'/'+name
        cmd('mkdir '+root)
        exchange(b'python\r', suffix='>>> ')
        py('f=open('+repr(root+'/text.txt')+',"x"); f.write("".join("line %03d hello\\n"%i for i in range(100))); f.close()')
        py('f=open('+repr(root+'/table.csv')+',"x"); f.write(\'name,value\\n"alpha, one",2\\nbeta,3\\ngamma,5\\n\'); f.close()')
        exchange(b'\x04', shell=True)
        exchange(('less '+root+'/text.txt\r').encode(), expect='line 000 hello')
        exchange(b' ', expect='line 023 hello')
        exchange(b'/line 075\r', expect='line 075 hello')
        exchange(b'G', expect='line 099 hello')
        exchange(b'q', shell=True)
        exchange(('notes '+root+'/notes.md\r').encode(), expect='SPACE done')
        exchange(b'aRemember the keyboard\r', expect='Remember the keyboard')
        exchange(b' ', expect='saved')
        exchange(b'cHardware\r', expect='Hardware')
        exchange(b'aTest peripherals\r', expect='Test peripherals')
        exchange(b'q', shell=True)
        text = cmd('cat '+root+'/notes.md')
        assert '- [x] Remember the keyboard' in text and '## Hardware' in text and '- [ ] Test peripherals' in text, text
        exchange(('notes '+root+'/notes.md\r').encode(), expect='Test peripherals')
        exchange(b'\x1d', shell=True)
        exchange(('sheet '+root+'/table.csv\r').encode(), expect='alpha, one')
        exchange(b'=SUM(value)\r', expect='SUM(value)=10 n=3')
        exchange(b'=AVG(value)\r', expect='AVG(value)=3.33333 n=3')
        exchange(b'=COUNT(*)\r', expect='COUNT(*)=3')
        exchange(b'=BOGUS(value)\r', expect='formula?')
        exchange(b'q', shell=True)
    exchange(b'less man:app.notes\r', expect='notes')
    exchange(b'q', shell=True)
    for app in ('less', 'sheet'):
        assert 'not a file' in cmd(app+' /flash/'+name+'/missing')
    # Files -> pager/Sheet -> Files verifies retained parent context and input.
    exchange(('files /flash/'+name+'\r').encode(), expect='table.csv')
    def select(filename):
        exchange(b'\x1b[H')
        for _ in range(12):
            for row in range(2, 21):
                if screen.buffer[row][3].reverse:
                    entry = ''.join(screen.buffer[row][c].data for c in range(3, 28)).strip()
                    if entry == filename:
                        return
            exchange(b'\x1b[B')
        raise AssertionError('Files entry not found: '+filename)
    select('table.csv')
    exchange(b'\r', expect='alpha, one')
    exchange(b'\x1d', expect='F3 V-iew')
    select('text.txt')
    exchange(b'\x1bOR', expect='line 000 hello')  # F3 view
    exchange(b'\x1d', expect='F3 V-iew')
    exchange(b'q', shell=True)
    before = memory()
    for _ in range(20):
        for app, file in (('less','text.txt'), ('notes','notes.md'), ('sheet','table.csv')):
            exchange((app+' /flash/'+name+'/'+file+'\r').encode())
            exchange(b'\x1d', shell=True)
        cmd('setterm size 80 24')
        cmd('identity user '+original[0])
    after = memory()
    report['memory_before'], report['memory_after'] = before, after
    assert before == after, (before, after)
    assert 'open=0' in cmd('flash status')
    # Create only a missing startup file, then remove our exact fixture in finally.
    cmd('mkdir /flash/.shell')
    listing = cmd('ls /flash/.shell')
    assert 'ls: cannot' not in listing, listing
    exists = any(line.split() and line.split()[-1].rstrip('/') == 'startup'
                 for line in listing.splitlines())
    if not exists:
        exchange(b'python\r', suffix='>>> ')
        py('f=open("/flash/.shell/startup","x"); f.write("echo '+name+' startup\\n"); f.close()')
        startup_created = True
        exchange(b'\x04', shell=True)
    if startup_created:
        cmd('setterm startup flash')
        assert name+' startup' in reboot()
        conn.close()
        time.sleep(.3)
        conn = connect()
        assert name+' startup' not in exchange(shell=True)
        report['startup_once_per_boot'] = True
    else:
        report['startup_test'] = 'Existing startup file preserved; execution fixture skipped'
    report['passed'] = True
finally:
    if conn and conn.is_open:
        try:
            exchange(b'\x1d\r', shell=True)
            if startup_created:
                assert 'rm:' not in cmd('rm /flash/.shell/startup')
            if original:
                cmd('identity user '+original[0])
                cmd('identity hostname '+original[1])
                cmd('setterm size '+original[2]+' '+original[3])
                cmd('setterm startup '+original[4])
        except Exception as error:
            report['cleanup_error'] = repr(error)
            report['passed'] = False
        conn.close()
    args.log.write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k != 'commands'}, indent=2))
assert report['passed'], report.get('cleanup_error')
