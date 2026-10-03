#!/usr/bin/env python3
"""Device acceptance for Python ownership, offline libraries and cleanup."""
import json,re,time
from pathlib import Path
from install_teensy41_python_bundle import Board
b=Board();report=[]
def cmd(line):
    out=b.command(line,timeout=45);report.append({'command':line,'output':out});print(out,flush=True);return out
def memory():
    out=cmd('mem');m=re.search(r'Internal heap: (\d+) free / \d+ bytes; PSRAM: (\d+) free',out);assert m,out;return tuple(map(int,m.groups()))
try:
    # Only update the generated self-test, never user library files.
    b.install(Path('examples/teensy41/python/hardware_selftest.py'),'/sd/python-examples/hardware_selftest.py',True)
    b.install(Path('lib/teensy41/README.txt'),'/flash/lib/README.txt',True)
    out=cmd('python /sd/python-examples/hardware_selftest.py');assert '\nPY_HARDWARE_OFFLINE_PASS\n' in out,out
    baseline=memory()
    for _ in range(3):
        out=cmd('python /sd/python-examples/hardware_selftest.py');assert '\nPY_HARDWARE_OFFLINE_PASS\n' in out,out
        assert memory()==baseline
    out=b.python("from machine import Pin; p=Pin(28); raise ValueError('cleanup fixture')")
    assert 'ValueError' in out
    out=b.python("from machine import Pin; p=Pin(28);p.deinit();print('LEASE_RECOVERED')")
    assert '\nLEASE_RECOVERED\n' in out,out
    b.c.write(b'python -c "from machine import Pin;import time;p=Pin(28);time.sleep(60)"\r');time.sleep(.5);b.c.read(8192)
    b.c.write(b'\x03');out=b.read();assert 'KeyboardInterrupt' in out,out
    out=b.python("from machine import Pin;p=Pin(28);p.deinit();print('CANCEL_RECOVERED')")
    assert '\nCANCEL_RECOVERED\n' in out,out
    assert memory()==baseline
    print('PASS: offline imports, claims, reserved pins/address, limits, filesystem, timing, repeated/exception/cancel cleanup')
finally:
    b.close();Path('/tmp/teensy-python-hardware-device.json').write_text(json.dumps(report,indent=2)+'\n')
