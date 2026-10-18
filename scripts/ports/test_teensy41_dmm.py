#!/usr/bin/env python3
"""Live DMM demo acceptance: display takeover, controls, Python and FTP coexistence.

Requires an idle Teensy module board, mounted SD, LAN access, and no existing
ftpd job. All measurements are DEMO; no voltage is applied to ADC40. Creates and
removes one unique SD fixture directory. Does not synthesize NES electrical input.
"""
import argparse
import ftplib
import io
import json
import re
import threading
import time
import uuid
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_ftp import Console


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--leave-running', action='store_true')
    args = parser.parse_args()
    report = {'commands': [], 'checks': [], 'passed': False}
    ports = [p.device for p in list_ports.comports() if (p.vid, p.pid) == (0x16c0, 0x0483)]
    assert len(ports) == 1, ports
    root = '/sd/_dmm_' + uuid.uuid4().hex[:8]
    server = fixture = python = False
    ftp = None
    thread = None
    with serial.Serial(ports[0], 115200, timeout=.03, write_timeout=3, exclusive=True) as port:
        console = Console(port, report)
        def cmd(text):
            out = console.command(text)
            print(out, flush=True)
            return out
        def state():
            out = cmd('dmm status')
            assert 'DMM running source=DEMO' in out, out
            return dict(re.findall(r'([\w-]+)=([^\s]+)', out))
        def check(text):
            report['checks'].append(text)
            print('PASS:', text, flush=True)
        try:
            cmd('')
            assert 'ftpd stopped' in cmd('job status ftpd'), 'Existing ftpd must be left alone'
            cmd('dmm stop')
            pins = cmd('io claims')
            assert re.search(r'gpio\s+1\s+-1\s+board\s+amplifier enable', pins), pins
            assert not re.search(r'gpio\s+40\s+-1\s+board', pins), pins
            assert 'live input disabled' in cmd('dmm start')
            assert 'running in background' in cmd('dmm --demo')
            time.sleep(.5)
            first = state()
            assert first['range'] == '5V' and first['valid'] == 'yes'
            assert 'small=owned' in cmd('lcd small')
            check('AmpEn1/ADC40 pin map, live-input gate and small-screen takeover')
            cmd('dmm hold'); held = state()
            time.sleep(.7); after = state()
            assert after['hold'] == 'on' and after['value-mV'] == held['value-mV']
            assert int(after['samples']) >= int(held['samples']) + 10
            cmd('dmm max'); assert state()['mode'] == 'MAX'
            cmd('dmm min'); assert state()['mode'] == 'MIN'
            cmd('dmm live'); cmd('dmm reset')
            cmd('dmm range 50'); time.sleep(.15); assert state()['range'] == '50V'
            cmd('dmm range 5')
            check('hold retains readout while samples advance; min/max/reset and both ranges')
            # Wait for the normal Ethernet/Wi-Fi startup to settle.
            cmd('network up')
            deadline = time.monotonic() + 25
            while True:
                out = cmd('network status')
                match = re.search(r'^\s+(?:eth0|wifi0)\s+up\s+(\d+\.\d+\.\d+\.\d+)', out, re.M)
                if match and match[1] != '0.0.0.0':
                    host = match[1]; break
                assert time.monotonic() < deadline, out
                time.sleep(1)
            assert 'mounted' in cmd('sd status')
            assert 'mkdir:' not in cmd('mkdir ' + root); fixture = True
            assert 'ftpd: OK' in cmd('job start ftpd ' + root + ' 2122 --user dmm --password dmm-test')
            server = True
            ftp = ftplib.FTP(); ftp.connect(host, 2122, timeout=20); ftp.login('dmm', 'dmm-test')
            payload = bytes(range(256)) * 1024
            transfer = {'passed': False}
            class SlowReader(io.BytesIO):
                def read(self, size=-1):
                    data = super().read(size)
                    if data: time.sleep(.02)
                    return data
            def exchange():
                try:
                    ftp.storbinary('STOR payload.bin', SlowReader(payload), blocksize=4096)
                    received = io.BytesIO()
                    ftp.retrbinary('RETR payload.bin', received.write)
                    assert received.getvalue() == payload, 'FTP payload mismatch'
                    transfer['passed'] = True
                except BaseException as exc:
                    transfer['error'] = repr(exc)
            before = state()
            thread = threading.Thread(target=exchange, daemon=True); thread.start()
            out = console.command('python', python=True); python = True
            assert out.endswith('>>> '), out
            code = "value=0\nfor i in range(2000000):\n value=(value+i)&65535\nprint('DMM_PY_DONE',value)"
            out = console.command('exec(' + repr(code) + ')', python=True)
            print(out, flush=True)
            assert 'DMM_PY_DONE' in out and 'Traceback' not in out, out
            port.write(b'\x04'); time.sleep(.3); port.read_all(); python = False
            thread.join(timeout=30)
            assert not thread.is_alive() and transfer['passed'], transfer
            after = state()
            assert int(after['samples']) > int(before['samples']) + 5, (before, after)
            assert after['read-errors'] == '0'
            report['concurrency'] = dict(before=before, after=after, ftp_bytes=len(payload))
            check('DMM samples advance during CPU-bound Python and exact 256 KiB FTP round trip')
            ftp.delete('payload.bin'); ftp.quit(); ftp = None
            cmd('job stop ftpd'); server = False
            cmd('rm -r ' + root); fixture = False
            assert 'small display restored' in cmd('dmm stop')
            assert 'small=owned' not in cmd('lcd small')
            check('stop releases the display back to the dashboard/fallback terminal')
            cmd('dmm --demo'); time.sleep(.3); state()
            cmd('dmm stop')
            check('repeat start/stop succeeds without stale display ownership')
            if args.leave_running: cmd('dmm --demo')
            report['passed'] = True
        finally:
            if thread and thread.is_alive(): thread.join(timeout=20)
            if ftp:
                try: ftp.close()
                except Exception: pass
            if python:
                try: port.write(b'\x03\x04'); time.sleep(.2); port.read_all()
                except Exception: pass
            if server:
                try: cmd('job stop ftpd')
                except Exception: pass
            if fixture:
                try: cmd('rm -r ' + root)
                except Exception: pass
            if not (args.leave_running and report['passed']):
                try: cmd('dmm stop')
                except Exception: pass
            args.log.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
