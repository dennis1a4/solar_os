#!/usr/bin/env python3
"""USB integration tests; write only a newly created, unique SD test directory."""
import argparse
import json
import re
import time
import termios
import uuid
from pathlib import Path
import serial
from serial.tools import list_ports

ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
PROMPT = "user@teensy41:/ "


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port')
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--repeat', type=int, default=20)
    parser.add_argument('--verify-existing', help='Previously created /_solaros_test_<hex> directory')
    parser.add_argument('--reboot', action='store_true', help='Reboot before verifying existing files')
    args = parser.parse_args()
    if args.verify_existing and not re.fullmatch(r'/_solaros_test_[0-9a-f]{10}', args.verify_existing):
        parser.error('invalid test directory')
    if args.reboot and not args.verify_existing:
        parser.error('--reboot requires --verify-existing')
    root = args.verify_existing or '/_solaros_test_' + uuid.uuid4().hex[:10]
    report = dict(directory=root, passed=False, commands=[])
    try:
        ports = [p.device for p in list_ports.comports() if (p.vid, p.pid) == (0x16c0, 0x0483)]
        port = args.port or (ports[0] if len(ports) == 1 else None)
        assert port, f'Expected one Teensy: {ports}'
        with serial.Serial(port, 115200, timeout=.05, write_timeout=2, exclusive=True) as conn:
            time.sleep(1)
            conn.reset_input_buffer()

            def exchange(raw, suffix=PROMPT, contains=None, quiet=False):
                conn.write(raw)
                data = bytearray()
                last_data = time.monotonic()
                deadline = last_data + 15
                while time.monotonic() < deadline:
                    chunk = conn.read(4096)
                    if chunk:
                        data.extend(chunk)
                        last_data = time.monotonic()
                    text = ANSI.sub('', data.decode(errors='replace')).replace('\r', '')
                    if (quiet and time.monotonic() - last_data > .3) or (not quiet and text.endswith(suffix)):
                        report['commands'].append(dict(input=repr(raw), output=data.decode(errors='replace')))
                        if contains is not None:
                            assert contains in text, f'Missing {contains!r}: {text[-1500:]!r}'
                        return text
                raise RuntimeError(f'Timeout: {raw!r}, output={data[-1500:]!r}')

            def cmd(text, contains=None):
                return exchange((text+'\r').encode(), contains=contains)

            def py(text, contains=None):
                return exchange((text+'\r').encode(), suffix='>>> ', contains=contains)

            cmd('')
            if args.verify_existing:
                if args.reboot:
                    conn.write(b'reboot\r')
                    conn.flush()
                    conn.close()
                    time.sleep(2)
                    deadline = time.monotonic() + 20
                    while True:
                        ports = [p.device for p in list_ports.comports() if (p.vid, p.pid) == (0x16c0, 0x0483)]
                        if len(ports) == 1:
                            conn.port = ports[0]
                            try:
                                conn.open()
                                time.sleep(1)
                                conn.reset_input_buffer()
                                break
                            except (serial.SerialException, OSError, termios.error):
                                conn.close()
                        if time.monotonic() > deadline:
                            raise RuntimeError('USB did not reappear after reboot')
                        time.sleep(.1)
                    cmd('')
                cmd(f'cat {root}/hello.py', 'print("Hello from edited SD")')
                cmd(f'python {root}/hello.py', '\nHello from edited SD\n')
                cmd(f'cat {root}/data.txt', '\nx\n')
                report.update(passed=True, reboot=args.reboot, uptime=cmd('uptime'))
                print(f'PASS: persisted SD files and Python execution after reboot={args.reboot}', flush=True)
                return
            exchange(b'python\r', suffix='>>> ', contains='MicroPython on SolarOS')
            py('6 * 7', '\n42\n')
            py('import gc, sys, math, json')
            py("print(json.dumps({'v': math.sqrt(81)}))", '9.0')
            exchange(b'\x04')
            assert 'mkdir:' not in cmd(f'mkdir {root}')
            cmd(f'ls {root}')
            # Create a script with the actual full-screen editor, save, exit.
            exchange(f'edit {root}/hello.py\r'.encode(), quiet=True)
            exchange(b'print("Hello from edited SD")\r', quiet=True)
            exchange(b'\x13', quiet=True, contains='saved')
            exchange(b'\x1d')
            cmd(f'cat {root}/hello.py', 'print("Hello from edited SD")')
            cmd(f'python {root}/hello.py', '\nHello from edited SD\n')
            print('PASS: editor save and Python SD script', flush=True)
            # Save over an existing file, then reopen; exercise the backup swap.
            exchange(f'edit {root}/hello.py\r'.encode(), quiet=True)
            exchange(b'\x01print("Hello from edited SD")\r\x13', quiet=True, contains='saved')
            exchange(b'\x1d')
            cmd(f'python {root}/hello.py', '\nHello from edited SD\n')
            # Dirty exit must ask, not silently discard.
            exchange(f'edit {root}/hello.py\r'.encode(), quiet=True)
            exchange(b'#unsaved\x1d', quiet=True, contains='Save')
            exchange(b'n')
            cmd(f'cat {root}/hello.py', 'print("Hello from edited SD")')
            cmd(f'cp {root}/hello.py {root}/copy.py')
            cmd(f'cp {root}/hello.py {root}/copy.py', 'cp:')
            cmd(f'cp {root}/hello.py {root}/HELLO.PY', 'cp:')
            cmd(f'mv {root}/copy.py {root}/hello.py', 'mv:')
            cmd(f'mv {root}/copy.py {root}/moved.py')
            cmd(f'cat {root}/moved.py', 'print("Hello from edited SD")')
            cmd(f'rm {root}/moved.py')
            # Raw descriptor bridge: write, append, update, seek, close and import.
            exchange(b'python\r', suffix='>>> ')
            py('import sys, gc')
            py(f"p='{root}/data.txt'")
            py("f=open(p,'w'); print(f.write('abc')); f.close()", '\n3\n')
            py("f=open(p,'a'); f.write('def'); f.close()")
            py("f=open(p,'r+'); f.seek(1); f.write('Z'); f.flush(); f.seek(0); print(f.read()); f.close()", 'aZcdef')
            py("f=open(p,'w'); f.write('x'); f.close(); f=open(p); print(f.read()); f.close()", '\nx\n')
            py("open(p,'x')", 'OSError')
            py(f"open('{root}/missing')", 'OSError')
            py(f"f=open('{root}/helper.py','w'); f.write('answer=42'); f.close()")
            py(f"sys.path.insert(0,'{root}'); import helper; print(helper.answer)", '\n42\n')
            py("a=bytearray(300000); a[299999]=123; gc.collect(); print(a[299999]); del a", '\n123\n')
            # Multiline input and Ctrl-C cancellation of a running infinite loop.
            exchange(b'while True:\r', suffix='... ')
            exchange(b'    pass\r', suffix='... ')
            conn.write(b'\r')
            time.sleep(.2)
            exchange(b'\x03', suffix='>>> ', contains='KeyboardInterrupt')
            py('print(7*8)', '\n56\n')
            py('1/0', 'ZeroDivisionError')
            py('bytearray(2000000)', 'MemoryError')
            exchange(b'def recurse():\r', suffix='... ')
            exchange(b'    recurse()\r', suffix='... ')
            py('')
            py('recurse()', 'RuntimeError')
            py('print(6*7)', '\n42\n')
            # Finalizers reclaim descriptors even if the app exits with open files.
            py("held=[open(p) for i in range(16)]")
            py("open(p)", 'OSError')
            exchange(b'\x04')
            exchange(b'python\r', suffix='>>> ')
            py(f"f=open('{root}/data.txt'); print(f.read()); f.close()", '\nx\n')
            exchange(b'\x04')
            # Failed-save recovery file blocks overwrite and preserves the original.
            cmd(f'cp {root}/hello.py {root}/hello.py.edit-tmp')
            exchange(f'edit {root}/hello.py\r'.encode(), quiet=True)
            exchange(b'changed\x13', quiet=True, contains='save blocked')
            exchange(b'\x1d', quiet=True, contains='Save')
            exchange(b'n')
            cmd(f'python {root}/hello.py', '\nHello from edited SD\n')
            cmd(f'rm {root}/hello.py.edit-tmp')
            # Match geometry for a taller serial terminal; return to default.
            cmd('setterm size 100 40')
            exchange(f'edit {root}/hello.py\r'.encode(), quiet=True)
            exchange(b'\x1d')
            cmd('setterm size 80 24')
            cmd('python -c "print(12*12)"', '\n144\n')
            before = cmd('mem')
            for i in range(args.repeat):
                cmd(f'python {root}/hello.py', '\nHello from edited SD\n')
                exchange(f'edit {root}/hello.py\r'.encode(), quiet=True)
                exchange(b'\x1d')
            after = cmd('mem')
            assert re.search(r'Internal heap:.*', before)[0] == re.search(r'Internal heap:.*', after)[0], (before, after)
            report['memory'] = after
            report['uptime'] = cmd('uptime')
            report['passed'] = True
            print(f'PASS: writable SD, editor, MicroPython, {args.repeat} lifecycle cycles; retained {root}', flush=True)
    except Exception as exc:
        report['error'] = str(exc)
        raise
    finally:
        args.log.write_text(json.dumps(report, indent=2)+'\n')
        print(f'Log: {args.log}')


if __name__ == '__main__':
    main()
