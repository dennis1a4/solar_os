#!/usr/bin/env python3
"""On-device synth checks; requires teensy41_synth firmware and audio shield.
Produces sound at low volume. No filesystem fixtures or settings are changed.
PCM telemetry validates generation, not analog headphone output.
"""
import argparse
import json
import re
import time
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_shell import ANSI


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', required=True, type=Path)
    parser.add_argument('--port')
    parser.add_argument('--disconnect-only', action='store_true')
    args = parser.parse_args()
    report = {'passed': False, 'commands': []}
    ports = [p.device for p in list_ports.comports() if (p.vid, p.pid) == (0x16c0, 0x0483)]
    port = args.port or (ports[0] if len(ports) == 1 else None)
    if not port:
        raise RuntimeError('Expected one Teensy serial port')
    prompt = re.compile(r'[\w.-]+@[\w.-]+:/[^\n]* $')
    try:
        with serial.Serial(port, 115200, timeout=.02, write_timeout=3, exclusive=True) as conn:
            def exchange(raw=b'', shell=False):
                conn.write(raw)
                output = bytearray()
                last = time.monotonic()
                deadline = last + 10
                while time.monotonic() < deadline:
                    data = conn.read(8192)
                    if data:
                        output.extend(data)
                        last = time.monotonic()
                    text = ANSI.sub('', output.decode(errors='replace')).replace('\r', '')
                    assert 'Fault IRQ:' not in text, text
                    if (bool(prompt.search(text)) if shell else time.monotonic()-last > .2):
                        report['commands'].append({'input': repr(raw), 'output': text})
                        return text
                raise AssertionError(f'Timeout: {raw!r}: {output[-2000:]!r}')

            def cmd(line):
                return exchange((line+'\r').encode(), shell=True)

            def status():
                text = exchange(b'?')
                assert 'synth: ESP_' not in text, text
                fields = dict(re.findall(r'(\w+)=([0-9]+)(?=\s|$)', text))
                assert 'voices' in fields, text
                return {key: int(value) for key, value in fields.items()}

            def launch():
                assert 'Synth:' in exchange(b'synth\r')
                exchange(b'--')  # 10% headphone volume

            def disconnect_check():
                launch()
                exchange(b'Hads')
                assert status()['voices'] == 3
                conn.close()
                time.sleep(.5)
                conn.open()
                cmd('')  # reconnect must return to the shell, without held notes
                first = cmd('audio status')
                time.sleep(.25)
                second = cmd('audio status')
                blocks = lambda text: int(re.search(r'blocks=(\d+)', text)[1])
                assert blocks(first) == blocks(second), (first, second)
                launch()
                exchange(b'Hads')
                s = status()
                assert s['voices'] == 3 and s['errors'] == 0, s
                exchange(b'\x03', shell=True)
                return cmd('audio status')

            cmd('')
            assert 'SGTL5000=ready' in cmd('audio status')
            if args.disconnect_only:
                report.update(audio=disconnect_check(), passed=True)
                print('PASS: USB disconnect silences synth, reconnect returns to shell, next synth works')
                return
            launch()
            # Pulsed terminal notes release without key-up events.
            exchange(b'a')
            time.sleep(.5)
            assert status()['voices'] == 0
            exchange(b'H')
            for wave in b'12345':
                exchange(bytes([wave])+b'a')
                time.sleep(.25)
                s = status()
                assert s['voices'] == 1 and s['peak'] > 0 and s['running'] == 1, s
                assert s['errors'] == 0, s
                exchange(b'a')
                time.sleep(.2)
                assert status()['voices'] == 0
            exchange(b'4asdfghjk')
            time.sleep(1)
            s = status()
            assert s['voices'] == 8 and s['peak'] > 0, s
            # Enable both oscillators and the resonant filter under full load.
            exchange(b']]]],,,,,,,,,,,,,,,,,,,,]........]..........')
            time.sleep(2)
            busy = status()
            assert busy['voices'] == 8 and busy['errors'] == 0, busy
            assert busy['misses'] == 0 and busy['underruns'] == 0, busy
            exchange(b' ')
            time.sleep(.5)
            assert status()['voices'] == 0
            exchange(b'q', shell=True)
            assert 'one second' in cmd('audio tone')  # independent producer must still start
            time.sleep(1.1)
            before = re.search(r'Internal heap:.*', cmd('mem'))[0]
            for i in range(20):
                launch()
                exchange(b'Hads')
                assert status()['voices'] == 3
                exchange(b'\x1d' if i % 2 else b'q', shell=True)
            after = re.search(r'Internal heap:.*', cmd('mem'))[0]
            assert before == after, (before, after)
            report.update(passed=True, full_polyphony=busy, memory=after,
                          audio=cmd('audio status'), uptime=cmd('uptime'))
            print('PASS: waveforms, pulse/hold, eight voices, filter/oscillator2, exits and 20 clean restarts')
    finally:
        args.log.write_text(json.dumps(report, indent=2)+'\n')


if __name__ == '__main__':
    main()
