#!/usr/bin/env python3
"""Exercise USB storage through the Teensy console; only writes unique folders.
No formatting, rebooting or physical-removal simulation. Removes successful
fixture folders and leaves the drive mounted at the end.
"""
from teensy41_fixture_cleanup import cleanup_fixtures
import argparse
import json
import re
import time
import uuid
from pathlib import Path

import serial
from serial.tools import list_ports

ANSI = re.compile(r'\x1b\[[0-?]*[ -/]*[@-~]')
PROMPT = re.compile(r'[\w.-]+@[\w.-]+:/[^\n]* $')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--keep-fixtures', action='store_true', help='Retain successful test files for inspection/persistence checks')
    p.add_argument('--log', type=Path, required=True)
    p.add_argument('--no-drive', action='store_true')
    args = p.parse_args()
    report = dict(passed=False, commands=[])
    try:
        ports = [p.device for p in list_ports.comports() if (p.vid, p.pid) == (0x16c0, 0x0483)]
        assert len(ports) == 1, ports
        with serial.Serial(ports[0], 115200, timeout=.05, write_timeout=3, exclusive=True) as c:
            time.sleep(.5)

            def exchange(command, python=False):
                c.write((command + '\r').encode())
                data = b''
                deadline = time.monotonic() + 30
                while time.monotonic() < deadline:
                    data += c.read(16384)
                    text = ANSI.sub('', data.decode(errors='replace')).replace('\r', '')
                    if (text.endswith('>>> ') if python else PROMPT.search(text)):
                        report['commands'].append(dict(command=command, output=text))
                        return text
                raise AssertionError((command, data[-2000:]))

            def py(command):
                text = exchange(command, True)
                assert 'Traceback' not in text, text
                return text

            exchange('')
            exchange('cd /')
            status = exchange('usb')
            listing = exchange('ls /')
            assert 'sd' in listing and 'flash' in listing, listing
            if args.no_drive:
                assert 'no drive' in status, status
                assert 'usb' not in listing, listing
                assert 'usb' in exchange('commands')
                assert 'ls:' in exchange('ls /usb')
                report['passed'] = True
                return
            assert '/usb' in status, status
            assert 'usb' in listing, listing
            report['status'] = status
            name = '_usb_test_' + uuid.uuid4().hex[:8]
            report['folder'] = name
            roots = ['/usb/' + name, '/sd/' + name, '/flash/' + name]
            for root in roots:
                text = exchange('mkdir ' + root)
                assert 'mkdir:' not in text, text
            for base in roots[1:]:
                assert 'mkdir:' not in exchange(f'mkdir {base}/usb')
            exchange('python', True)
            py('import gc')
            py('data=bytes(range(256))*32')
            for root in roots:
                py(f"f=open('{root}/data.bin','wb'); assert f.write(data)==len(data); f.close()")
                py(f"f=open('{root}/data.bin','rb'); assert f.read()==data; f.close()")
            root = roots[0]
            py(f"p='{root}/long filename.txt'")
            py("f=open(p,'w'); f.write('abc'); f.close()")
            py("f=open(p,'a'); f.write('def'); f.close()")
            py("f=open(p,'r+'); f.seek(1); f.write('Z'); f.flush(); f.seek(0); assert f.read()=='aZcdef'; f.close()")
            # Nested names must stay on their original volume, never route to USB.
            for base in roots[1:]:
                py(f"f=open('{base}/usb/marker','w'); f.write('nested'); f.close()")
                py(f"f=open('{base}/usb/marker'); assert f.read()=='nested'; f.close()")
            for _ in range(20):
                py(f"f=open('{root}/data.bin','rb'); assert f.read()==data; f.close()")
            exchange('\x04')
            assert 'long filename.txt' in exchange(f'ls {root}')
            assert 'cp:' not in exchange(f'cp {root}/data.bin {roots[1]}/from_usb.bin')
            assert 'mv:' not in exchange(f'mv {roots[1]}/from_usb.bin {roots[2]}/from_usb.bin')
            assert 'cp:' in exchange(f'cp {root}/data.bin {root}/data.bin')
            assert 'cp:' not in exchange(f'cp {roots[1]}/data.bin {root}/native.bin')
            assert 'mv:' not in exchange(f'mv {root}/native.bin {root}/renamed.bin')
            assert 'rm:' not in exchange(f'rm {root}/renamed.bin')
            for _ in range(3):
                assert 'safe to unplug' in exchange('usb eject')
                assert 'usb' not in exchange('ls /')
                assert '/usb' in exchange('usb mount')
                exchange('python', True)
                py(f"f=open('{root}/data.bin','rb'); assert f.read()==bytes(range(256))*32; f.close()")
                exchange('\x04')
            # A file held by the LCD interpreter must prevent USB-console eject.
            exchange('lcd send "python"')
            time.sleep(.5)
            exchange(f'lcd send "f=open(\'{root}/data.bin\',\'rb\')"')
            time.sleep(.5)
            exchange('lcd send "d=f.read(); assert d==bytes(range(256))*32; print(123456789)"')
            time.sleep(.5)
            dump = exchange('lcd dump')
            assert 'Traceback' not in dump, dump
            assert '123456789' in [line.strip() for line in dump.splitlines()], dump
            assert 'busy' in exchange('usb eject')
            exchange('lcd key exit')
            time.sleep(.5)
            assert 'safe to unplug' in exchange('usb eject')
            assert '/usb' in exchange('usb mount')
            cleanup_fixtures(exchange, roots, report, args.keep_fixtures)
            report.update(passed=True, memory=exchange('mem'))
    except Exception as exc:
        report['error'] = str(exc)
        raise
    finally:
        args.log.write_text(json.dumps(report, indent=2) + '\n')
        print('PASS' if report['passed'] else 'FAIL', args.log)


if __name__ == '__main__':
    main()
