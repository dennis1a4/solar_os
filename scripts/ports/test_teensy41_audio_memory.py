#!/usr/bin/env python3
"""On-device audio ring lifecycle; unique SD fixtures are removed on success.
With no codec, checks clean failure and idle memory only. With a codec, also
checks repeated one-second silent WAV playback/recording and tone ring usage.
"""
import argparse
import json
import re
import time
import uuid
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_shell import ANSI


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--log', required=True, type=Path)
    args = p.parse_args()
    report = {'passed': False, 'commands': []}
    root = '/sd/_audio_ram_' + uuid.uuid4().hex[:8]
    report['fixture'] = root
    ports = [p.device for p in list_ports.comports() if (p.vid, p.pid) == (0x16c0, 0x0483)]
    assert len(ports) == 1, ports
    try:
        with serial.Serial(ports[0], 115200, timeout=.03, write_timeout=3, exclusive=True) as conn:
            def cmd(line):
                conn.write((line+'\r').encode())
                data = bytearray()
                last = time.monotonic()
                deadline = last + 25
                while time.monotonic() < deadline:
                    chunk = conn.read(16384)
                    if chunk:
                        data.extend(chunk)
                        last = time.monotonic()
                    out = ANSI.sub('', data.decode(errors='replace')).replace('\r', '')
                    assert 'Fault IRQ:' not in out, out
                    if re.search(r'[\w.-]+@[\w.-]+:/[^\n]* $', out) and time.monotonic()-last > .2:
                        report['commands'].append({'command': line, 'output': out})
                        return out
                raise AssertionError(out)

            def py(code):
                line = 'python -c ' + json.dumps(code)
                assert len(line) <= 191, line
                out = cmd(line)
                assert 'Traceback' not in out and 'Error' not in out, out

            def idle():
                status = cmd('audio status')
                assert 'playback=0 capture=0 bytes' in status, status
                out = cmd('mem')
                match = re.search(r'Internal heap: (\d+) free / \d+ bytes; PSRAM: (\d+) free', out)
                assert match, out
                return tuple(map(int, match.groups()))

            cmd('')
            status = cmd('audio status')
            ready = 'SGTL5000=ready' in status
            report['codec_ready'] = ready
            cmd('mkdir ' + root)
            # One second of mono silence; no copyrighted or user audio fixture.
            # Send the fixed WAV header as hex to stay within shell line limits.
            import struct
            header = struct.pack('<4sI4s4sIHHIIHH4sI', b'RIFF', 88236, b'WAVE',
                                 b'fmt ', 16, 1, 1, 44100, 88200, 2, 16, b'data', 88200)
            py("f=open(%r,'wb');f.close()" % (root+'/s.wav'))
            for offset in range(0, len(header), 16):
                py("import binascii;f=open(%r,'ab');f.write(binascii.unhexlify(%r));f.close()" %
                   (root+'/s.wav', header[offset:offset+16].hex()))
            py("f=open(%r,'ab');[f.write(bytes(882)) for _ in range(100)];f.close()" % (root+'/s.wav'))
            baseline = idle()
            report['memory_before'] = baseline
            for i in range(5):
                out = cmd('aplay -v 10 '+root+'/s.wav')
                assert ('ret=OK' if ready else 'NOT_FOUND') in out, out
                assert idle() == baseline
                path = root + '/r.wav'
                out = cmd('arecord -d 1 '+path)
                assert ('arecord: done' if ready else 'NOT_FOUND') in out, out
                cmd('rm '+path)
                assert idle() == baseline
            cmd('audio tone')
            assert idle() == baseline
            time.sleep(1.1)
            cmd('audio off')
            report['memory_after'] = idle()
            assert report['memory_after'] == baseline
            cmd('rm -r '+root)
            out = cmd('ls '+root)
            assert 'No such file or directory' in out, out
            report.update(passed=True, fixture_removed=True)
            print('PASS: '+('live playback/recording' if ready else 'missing-codec failure paths')+
                  ', zero idle ring bytes and exact heap recovery', flush=True)
            print(cmd('mem'), flush=True)
    finally:
        args.log.write_text(json.dumps(report, indent=2)+'\n')


if __name__ == '__main__':
    main()
