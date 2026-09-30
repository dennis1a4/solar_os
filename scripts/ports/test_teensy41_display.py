#!/usr/bin/env python3
"""Two-console hardware checks. Retains no test files; uses lcd diagnostics.
Requires exclusive USB serial access. Leave the local keyboard idle during test.
"""
import argparse,json,re,time
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_shell import ANSI
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--log',type=Path,required=True)
p.add_argument('--status-only',action='store_true')
a=p.parse_args()
report={'passed':False,'mode':'status' if a.status_only else 'full','commands':[]}
ports=[x.device for x in list_ports.comports() if (x.vid,x.pid)==(0x16c0,0x0483)]
assert len(ports)==1,ports
conn=serial.Serial(ports[0],115200,timeout=.03,write_timeout=3,exclusive=True)
prompt=re.compile(r'[\w.-]+@[\w.-]+:/[^\n]* $')
def exchange(raw=b'',shell=True,timeout=20):
    conn.write(raw); data=bytearray(); deadline=time.monotonic()+timeout; last=time.monotonic()
    while time.monotonic()<deadline:
        b=conn.read(8192)
        if b: data.extend(b); last=time.monotonic()
        text=ANSI.sub('',data.decode(errors='replace')).replace('\r','')
        assert 'Fault IRQ:' not in text,text
        if (shell and prompt.search(text)) or (not shell and time.monotonic()-last>.4):
            report['commands'].append({'input':repr(raw),'output':text})
            a.log.write_text(json.dumps(report,indent=2)+'\n')
            return text
    raise RuntimeError(f'timeout {raw!r}: {data[-1500:]!r}')
def cmd(s): return exchange((s+'\r').encode())
def local(s):
    assert 'Queued' in cmd('lcd send '+json.dumps(s))
    time.sleep(.5)
def dump(): return cmd('lcd dump')
def contains(s,timeout=15):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        text=dump()
        if s in text:return text
        time.sleep(.3)
    raise AssertionError((s,text))
def mem():
    s=cmd('mem'); m=re.search(r'Internal heap: (\d+) free / \d+ bytes; PSRAM: (\d+) free',s)
    assert m,s
    return tuple(map(int,m.groups()))
try:
    time.sleep(1); exchange(b'\x1d\r')
    status=cmd('lcd'); assert 'local consoles: usb and lcd' in status,status
    report['keyboard_status']=status
    if a.status_only:
        report['passed']=True
        print(status)
        raise SystemExit(0)
    cmd('lcd key exit'); time.sleep(.2)
    cmd('cd /'); local('cd /'); local('clear')
    local('echo LCD_ONLY_MARKER'); contains('LCD_ONLY_MARKER')
    cmd('echo USB_ONLY_MARKER'); assert 'USB_ONLY_MARKER' not in dump()
    local('cd /flash'); contains(':/flash')
    assert cmd('echo USB_DIRECTORY').endswith(':/ ')
    local('calc'); contains('calc')
    usb=exchange(b'calc\r',shell=False); assert 'INVALID_STATE' in usb or 'already' in usb or 'busy' in usb.lower(),usb
    cmd('lcd key exit'); time.sleep(.3)
    local('python'); contains('>>>')
    local('while True: pass'); local('')
    assert 'USB_STILL_RESPONDS' in cmd('echo USB_STILL_RESPONDS')
    cmd('lcd key ctrlc'); contains('KeyboardInterrupt')
    cmd('lcd key exit'); time.sleep(.3)
    local('wait 3'); assert 'CONCURRENT_WAIT' in cmd('echo CONCURRENT_WAIT'); time.sleep(3)
    local('cd /'); local('clear'); baseline=mem()
    for i in range(10):
        local('calc'); cmd('echo USB_DURING_CALC'); cmd('lcd key exit'); time.sleep(.2)
    after=mem(); assert after==baseline,(baseline,after)
    local('echo LCD_READY'); contains('LCD_READY')
    conn.close(); time.sleep(1)
    conn=serial.Serial(ports[0],115200,timeout=.03,write_timeout=3,exclusive=True)
    time.sleep(1); exchange(b'\r'); contains('LCD_READY')
    report.update(passed=True,memory_before=baseline,memory_after=after)
    print('PASS: independent output/directories, app ownership, Python cancellation, wait, restart cleanup, USB reconnect')
finally:
    conn.close(); a.log.write_text(json.dumps(report,indent=2)+'\n')
