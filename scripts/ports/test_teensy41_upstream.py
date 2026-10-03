#!/usr/bin/env python3
"""Integration smoke: task monitor, radio lifecycle, key generation, MIDI/PD status.
Requires idle consoles. Creates/removes a key ONLY when no default keys exist.
Does not open PD hardware or send MIDI to attached devices.
"""
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
    assert '4.15.18' in cmd('version')
    assert 'unconfigured' in cmd('pd status')
    assert 'operation=idle' in cmd('midi status')
    stations=cmd('webradio list');assert 'NO_MEM' not in stations and 'FAIL' not in stations,stations
    # Warm the radio catalog, then exercise both app lifecycles repeatedly.
    local('webradio');cmd('lcd key exit');time.sleep(.4)
    baseline=mem()
    for i in range(3):
        local('ltop');time.sleep(1.2)
        screen=cmd('lcd dump');assert 'CPU0' in screen and 'FREE' in screen,screen
        cmd('lcd key exit');time.sleep(.3)
        local('webradio');screen=cmd('lcd dump');assert 'radio' in screen.lower(),screen
        cmd('lcd key exit');time.sleep(.3)
    assert mem()==baseline,('app cleanup',baseline)
    status=cmd('sshkey status');assert '/flash/.ssh/' in status,status
    if status.count('(missing, 0 bytes)')==2:
        assert 'generated default RSA key' in cmd('sshkey gen 2048',180)
        public=cmd('sshkey pub');assert re.search(r'ssh-rsa [A-Za-z0-9+/=]+',public),public
        assert 'INVALID_STATE' in cmd('sshkey gen 2048',30)
        assert 'removed' in cmd('sshkey rm')
    else:
        log.append({'note':'Existing user key preserved; generation test skipped.'})
    # Exercise recording, cancellation and file lifetime without transmitting notes.
    mount='/mt'+uuid.uuid4().hex[:6]
    cmd('ramfs mount '+mount+' 64k')
    if 'USB MIDI: absent' in cmd('midi status'):
        assert 'NOT_FOUND' in cmd('midi record usb '+mount+'/absent.smr')
        listing=cmd('ls '+mount);assert 'absent.smr' not in listing,listing
    c.write(('midi record slot1 '+mount+'/empty.smr\r').encode())
    initial=read(.5,False);assert 'MIDI recording' in initial,initial
    c.write(b'\x03');stopped=read();assert 'midi:' in stopped and '(stopped)' in stopped,stopped
    exists=cmd('midi record slot1 '+mount+'/empty.smr');assert 'already exists' in exists,exists
    cmd('rm '+mount+'/empty.smr');cmd('ramfs unmount '+mount)
    assert '31250' in cmd('midi status')
    assert 'PSRAM reserve: 128 KiB' in cmd('mem')
    print('PASS: upstream version, ltop samples, radio catalog/lifecycle, exact app cleanup, SSH key commands, inactive hardware status')
finally:
    c.close();Path('/tmp/teensy-upstream-device.json').write_text(json.dumps(log,indent=2)+'\n')
