#!/usr/bin/env python3
"""Check bounded pipe paging on idle USB/LCD consoles; no files are changed."""
import json
import re
import time
import uuid
from pathlib import Path
import serial
from serial.tools import list_ports

ansi=re.compile(r'\x1b\[[0-?]*[ -/]*[@-~]')
prompt=re.compile(r'[\w.-]+@[\w.-]+:/[^\n]* $')
log=[]
ports=[p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x16c0,0x0483)]
assert len(ports)==1,ports
c=serial.Serial(ports[0],115200,timeout=.03,write_timeout=3,exclusive=True)
def read(seconds=15,shell=True):
    data=b'';end=time.monotonic()+seconds
    while time.monotonic()<end:
        data+=c.read(16384)
        text=ansi.sub('',data.decode(errors='replace')).replace('\r','')
        assert 'Fault IRQ:' not in text,text
        if shell and prompt.search(text):return text
    if shell:raise AssertionError(data[-2000:])
    return text

def cmd(s,timeout=15):
    c.write((s+'\r').encode());out=read(timeout);log.append({'command':s,'output':out});print(out,flush=True);return out

def local(s):
    assert 'Queued' in cmd('lcd send '+json.dumps(s));time.sleep(.5)

def mem():
    out=cmd('mem');m=re.search(r'Internal heap: (\d+) free / \d+ bytes; PSRAM: (\d+) free',out);assert m,out;return tuple(map(int,m.groups()))
try:
    time.sleep(1);cmd('')
    # Warm app state/history before measuring repeated release.
    local('commands | less');assert '[pipe]' in cmd('lcd dump')
    cmd('lcd key exit');time.sleep(.3)
    baseline=mem()
    for i in range(5):
        local('commands | less')
        first=cmd('lcd dump');assert '[pipe]' in first and 'commands' in first,first
        # Ownership rejection on the second console must not leak pipe buffers.
        assert 'already running' in cmd('commands | less')
        cmd('lcd key space');time.sleep(.2)
        second=cmd('lcd dump');assert second!=first
        cmd('lcd key exit');time.sleep(.2)
        assert mem()==baseline
    for line in ('echo PAGE_MARKER | less','echo x | grep absent | less'):
        c.write((line+'\r').encode());screen=read(.8,False)
        assert '[pipe]' in screen,screen
        c.write(b'q');read()
        assert mem()==baseline
    for line in ('echo x | less; echo WRONG','echo x | less && echo WRONG','echo x | less file'):
        out=cmd(line);assert 'must end the command line' in out,out
    cmd('lcd send "cd /flash"')
    print('PASS: commands pager, scroll, USB/LCD exit, empty input, ownership rejection, repeated memory recovery')
finally:
    c.close();Path('/tmp/teensy-less-device.json').write_text(json.dumps(log,indent=2)+'\n')
