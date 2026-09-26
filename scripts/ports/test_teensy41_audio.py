#!/usr/bin/env python3
"""Upload generated quiet tones through Python, then exercise Teensy aplay.

Requires the audio firmware, fitted PSRAM, mounted Teensy SD and wired shield.
Creates and retains one unique SD test directory. Never formats a card.
"""
import argparse
import base64
import hashlib
import json
import re
import time
import uuid
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_shell import ANSI, PROMPT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixtures', required=True, type=Path)
    parser.add_argument('--log', required=True, type=Path)
    parser.add_argument('--port')
    parser.add_argument('--repeat', type=int, default=20)
    args = parser.parse_args()
    names = ['stereo.mp3', 'mono48.mp3', 'mono22.wav']
    files = {name: (args.fixtures / name).read_bytes() for name in names}
    assert all(0 < len(data) < 100000 for data in files.values()), 'Use the generated one-second tones'
    root = '/_solaros_audio_' + uuid.uuid4().hex[:10]
    report = dict(directory=root, passed=False, commands=[])
    try:
        ports = [p.device for p in list_ports.comports() if (p.vid, p.pid) == (0x16c0, 0x0483)]
        port = args.port or (ports[0] if len(ports) == 1 else None)
        assert port, f'Expected one Teensy: {ports}'
        with serial.Serial(port, 115200, timeout=.02, write_timeout=3, exclusive=True) as conn:
            time.sleep(1)
            conn.reset_input_buffer()

            def exchange(raw, suffix=PROMPT, expected=None, record=True):
                if raw: conn.write(raw)
                data = bytearray()
                deadline = time.monotonic() + 15
                while time.monotonic() < deadline:
                    data.extend(conn.read(8192))
                    text = ANSI.sub('', data.decode(errors='replace')).replace('\r', '')
                    if text.endswith(suffix):
                        if record:
                            report['commands'].append(dict(input=repr(raw), output=text))
                        if expected is not None:
                            assert expected in text, f'Missing {expected!r}: {text[-1200:]!r}'
                        return text
                raise RuntimeError(f'Timeout for {raw!r}: {data[-1000:]!r}')

            def cmd(line, expected=None):
                return exchange((line+'\r').encode(), expected=expected)

            def py(line, expected=None, record=True):
                return exchange((line+'\r').encode(), suffix='>>> ', expected=expected, record=record)

            cmd('')
            cmd('audio status', 'SGTL5000=ready')
            cmd('audio tone', 'one second')
            time.sleep(1.1)
            assert 'mkdir:' not in cmd('mkdir '+root)
            exchange(b'python\r', suffix='>>> ')
            py('import binascii, hashlib')
            for name, data in files.items():
                py(f"f=open('{root}/{name}','wb')")
                for offset in range(0, len(data), 768):
                    chunk = base64.b64encode(data[offset:offset+768]).decode()
                    py(f"n=f.write(binascii.a2b_base64('{chunk}'))", record=False)
                py('f.close()')
                py(f"f=open('{root}/{name}','rb'); print(binascii.hexlify(hashlib.sha256(f.read()).digest()).decode()); f.close()",
                   hashlib.sha256(data).hexdigest())
            exchange(b'\x04')
            for name in names:
                start = time.monotonic()
                cmd(f'aplay -v 10 {root}/{name}', 'ret=OK')
                elapsed = time.monotonic() - start
                assert .85 < elapsed < 4, f'Wrong playback duration: {elapsed}'
                status = cmd('audio status')
                match = re.search(r'blocks=(\d+) underruns=(\d+)', status)
                assert match and int(match[1]) >= 340 and int(match[2]) == 0, status
                print(f'PASS: {name}, {elapsed:.3f}s, {match[1]} output blocks', flush=True)
            # Interrupt active decoding/output, then prove the next play still works.
            conn.write(f'aplay -v 10 {root}/stereo.mp3\r'.encode())
            time.sleep(.2)
            exchange(b'\x03', expected='ret=TIMEOUT')
            cmd(f'aplay -v 10 {root}/stereo.mp3', 'ret=OK')
            cmd(f'aplay {root}/missing.mp3', 'aplay: open failed:')
            cmd('aplay -v 101 bad.mp3', 'usage:')
            before = re.search(r'Internal heap:.*', cmd('mem'))[0]
            for i in range(args.repeat):
                cmd(f'aplay -v 10 {root}/{names[i % len(names)]}', 'ret=OK')
            after = re.search(r'Internal heap:.*', cmd('mem'))[0]
            assert before == after, (before, after)
            report.update(passed=True, memory=after, uptime=cmd('uptime'), repeats=args.repeat)
            print(f'PASS: audio streaming, cancellation, errors and {args.repeat} cycles. Files: {root}', flush=True)
    except Exception as exc:
        report['error'] = str(exc)
        raise
    finally:
        args.log.write_text(json.dumps(report, indent=2)+'\n')
        print(f'Log: {args.log}')


if __name__ == '__main__':
    main()
