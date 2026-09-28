#!/usr/bin/env python3
"""Exercise the upstream Files TUI on Teensy. Requires pyserial and pyte.
Writes only unique SD/flash fixture directories and retains them for inspection.
"""
import argparse
import io
import json
import re
import time
import uuid
import zipfile
from pathlib import Path

import pyte
import serial
from serial.tools import list_ports
from test_teensy41_shell import ANSI, PROMPT

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--log', type=Path, required=True)
a = p.parse_args()
name = '_solaros_files_' + uuid.uuid4().hex[:8]
sd, flash = '/sd/' + name, '/flash/' + name
report = {'passed': False, 'directory': name, 'commands': []}
screen = pyte.Screen(80, 24)
stream = pyte.Stream(screen)
try:
    ports = [v.device for v in list_ports.comports() if (v.vid, v.pid) == (0x16c0, 0x0483)]
    assert len(ports) == 1, ports
    with serial.Serial(ports[0], 115200, timeout=.02, write_timeout=3, exclusive=True) as conn:
        time.sleep(1)
        conn.reset_input_buffer()

        def exchange(raw, suffix=None, expect=None, timeout=30):
            conn.write(raw)
            data = bytearray()
            deadline = time.monotonic() + timeout
            last = time.monotonic()
            while time.monotonic() < deadline:
                chunk = conn.read(8192)
                if chunk:
                    data.extend(chunk)
                    stream.feed(chunk.decode(errors='replace'))
                    last = time.monotonic()
                text = ANSI.sub('', data.decode(errors='replace')).replace('\r', '')
                display = '\n'.join(screen.display)
                if 'Fault IRQ:' in text:
                    raise RuntimeError(text)
                ready = text.endswith(suffix) if suffix else time.monotonic() - last > .2
                if ready and (expect is None or expect in display or expect in text):
                    report['commands'].append({'input': repr(raw), 'output': text, 'screen': display})
                    a.log.write_text(json.dumps(report, indent=2) + '\n')
                    return text
            raise RuntimeError(f'Timeout: {raw!r}: {data[-1500:]!r}\n{display}')

        def cmd(s): return exchange((s + '\r').encode(), suffix=PROMPT)
        def py(s):
            text = exchange((s + '\r').encode(), suffix='>>> ')
            assert 'Traceback' not in text, text
            return text

        def select(name, pane=0):
            exchange(b'\x1b[H')
            col = 3 if pane == 0 else 43
            for _ in range(250):
                for row in range(2, 21):
                    if screen.buffer[row][col].reverse:
                        entry = ''.join(screen.buffer[row][c].data for c in range(col, col + 25)).strip()
                        if entry.rstrip('/') == name:
                            return
                exchange(b'\x1b[B')
            raise AssertionError(f'Entry not found: {name}\n' + '\n'.join(screen.display))

        conn.write(b'\x1d')
        time.sleep(.15)
        conn.reset_input_buffer()
        cmd('cd /')
        assert 'files' in cmd('apps')
        for root in (sd, flash, sd + '/tree'):
            assert 'mkdir:' not in cmd('mkdir ' + root)
        exchange(b'python\r', suffix='>>> ')
        py('f=open(' + repr(sd + '/alpha.txt') + ',"x"); f.write("original\\n"); f.close()')
        py('f=open(' + repr(sd + '/move.bin') + ',"xb"); f.write(bytes(range(256))*257); f.close()')
        py('f=open(' + repr(sd + '/tree/child.txt') + ',"x"); f.write("nested contents"); f.close()')
        py('exec(' + repr('f=open(' + repr(sd + '/cancel.bin') + ',"xb")\nfor i in range(32):\n f.write(bytes(range(256))*128)\nf.close()') + ')')
        py('f=open(' + repr(sd + '/bad.py') + ',"x"); f.write("this is invalid Python !!!"); f.close()')
        py('f=open(' + repr(flash + '/return.bin') + ',"xb"); f.write(bytes(range(256))*257); f.close()')
        py('f=open(' + repr(flash + '/returnmove.txt') + ',"x"); f.write("from flash"); f.close()')
        exchange(b'\x04', suffix=PROMPT)
        report['memory_before'] = cmd('mem')
        exchange(('files ' + sd + '\r').encode(), expect='alpha.txt')
        assert '9B' in '\n'.join(screen.display), '64-bit file-size formatting failed'
        assert 'luB' not in '\n'.join(screen.display)
        # Right pane: up through /sd to the virtual root, then into flash.
        exchange(b'\t\x7f\x7f', expect='flash/')
        select('flash', 1)
        exchange(b'\r', expect=name)
        select(name, 1)
        exchange(b'\r')
        select('return.bin', 1)
        exchange(b'c', expect='copied')
        select('returnmove.txt', 1)
        exchange(b'm', expect='moved')
        exchange(b'\t')
        select('alpha.txt')
        exchange(b'e', expect='alpha.txt')
        exchange(b'edited \x13', expect='saved')
        exchange(b'\x1d', expect='F3 V-iew')
        assert sd in '\n'.join(screen.display) and flash in '\n'.join(screen.display)
        exchange(b'c', expect='copied')
        select('move.bin')
        exchange(b'm', expect='moved')
        select('tree')
        exchange(b'c', expect='copied')
        select('cancel.bin')
        exchange(b'c\x1b', expect='cancelled')
        select('bad.py')
        failed_child = exchange(b'\r', expect='F3 V-iew')
        assert 'SyntaxError' in failed_child, failed_child
        select('alpha.txt')
        exchange(b'z')
        exchange(b'\x7f' * 60 + b'bundle.zip\r', expect='zipped')
        exchange(b'\t')
        exchange(b'nnewdir\r', expect='newdir')
        select('newdir', 1)
        exchange(b'd', expect='delete newdir?')
        exchange(b'y', expect='deleted')
        # Opening and closing a child repeatedly must restore the parent screen.
        exchange(b'\t')
        select('alpha.txt')
        for _ in range(5):
            exchange(b'e', expect='alpha.txt')
            exchange(b'\x1d', expect='F3 V-iew')
        exchange(b'q', suffix=PROMPT)
        exchange(b'python\r', suffix='>>> ')
        py('import binascii')
        py('f=open(' + repr(sd + '/alpha.txt') + '); assert f.read()=="edited original\\n"; f.close()')
        py('f=open(' + repr(flash + '/alpha.txt') + '); assert f.read()=="edited original\\n"; f.close()')
        py('f=open(' + repr(flash + '/move.bin') + ',"rb"); assert f.read()==bytes(range(256))*257; f.close()')
        py('f=open(' + repr(flash + '/tree/child.txt') + '); assert f.read()=="nested contents"; f.close()')
        py('f=open(' + repr(sd + '/return.bin') + ',"rb"); assert f.read()==bytes(range(256))*257; f.close()')
        py('f=open(' + repr(sd + '/returnmove.txt') + '); assert f.read()=="from flash"; f.close()')
        for absent in (flash + '/returnmove.txt', flash + '/cancel.bin'):
            py('exec(' + repr('try:\n open(' + repr(absent) + ')\n raise AssertionError("file remains")\nexcept OSError:\n pass') + ')')
        py('exec(' + repr('try:\n open(' + repr(sd + '/move.bin') + ')\n raise AssertionError("source remains")\nexcept OSError:\n pass') + ')')
        text = py('f=open(' + repr(flash + '/bundle.zip') + ',"rb"); print("ZIPHEX:"+binascii.hexlify(f.read()).decode()); f.close()')
        data = next(line[len('ZIPHEX:'):] for line in text.splitlines() if line.startswith('ZIPHEX:'))
        with zipfile.ZipFile(io.BytesIO(bytes.fromhex(data))) as archive:
            assert archive.read('alpha.txt') == b'edited original\n', archive.namelist()
        exchange(b'\x04', suffix=PROMPT)
        assert 'newdir' not in cmd('ls ' + flash)
        # Warm the same app sequence before comparing heap/PSRAM cleanup.
        report['memory_after'] = cmd('mem')
        def free_memory(text):
            match = re.search(r'Internal heap: (\d+) free / \d+ bytes; PSRAM: (\d+) free', text)
            assert match, text
            return tuple(map(int, match.groups()))
        before = free_memory(report['memory_before'])
        after = free_memory(report['memory_after'])
        # The internal allocator may reclaim/coalesce space during the workload.
        # A leak consumes free space; a small increase is not a failure.
        assert after[0] >= before[0] and after[1] == before[1], (before, after)
        for _ in range(5):
            exchange(b'files /\r', expect='flash/')
            exchange(b'q', suffix=PROMPT)
        restarted = free_memory(cmd('mem'))
        assert restarted[0] >= after[0] and restarted[1] == after[1], (after, restarted)
        assert 'open=0' in cmd('flash status')
        report['uptime'] = cmd('uptime')
        report['passed'] = True
        print('PASS: Files mount navigation, SD/flash copy/move, recursive copy, mkdir/delete, ZIP, editor return and cleanup')
finally:
    report['last_screen'] = '\n'.join(screen.display)
    a.log.write_text(json.dumps(report, indent=2) + '\n')
