#!/usr/bin/env python3
"""Retained app acceptance. Requires idle keyboard/exclusive USB; creates a unique SD fixture."""
import argparse,json,re,time,uuid
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_hotplug import ANSI
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--log',type=Path,required=True)
p.add_argument('--cleanup-id',type=int,help='Close a known test session left by an interrupted run')
a=p.parse_args()
ports=[p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x16c0,0x0483)]
assert len(ports)==1,ports
conn=serial.Serial(ports[0],115200,timeout=.03,write_timeout=3,exclusive=True)
report={'passed':False,'commands':[]}
def exchange(raw=b'',settle=.35,timeout=20,prompt=False):
    conn.write(raw);data=bytearray();start=last=time.monotonic()
    while time.monotonic()-start<timeout:
        chunk=conn.read(16384)
        if chunk:data.extend(chunk);last=time.monotonic()
        plain=ANSI.sub('',data.decode(errors='replace')).replace('\r','')
        ready=not prompt or re.search(r'[\w.-]+@[\w.-]+:/[^\n]* $',plain)
        if data and ready and time.monotonic()-last>settle:break
    out=ANSI.sub('',data.decode(errors='replace')).replace('\r','')
    assert 'Fault IRQ:' not in out,out
    if prompt:assert re.search(r'[\w.-]+@[\w.-]+:/[^\n]* $',out),out
    report['commands'].append({'input':repr(raw),'output':out})
    a.log.write_text(json.dumps(report,indent=2)+'\n')
    return out
def cmd(s):return exchange((s+'\r').encode(),prompt=s.split()[0] not in ('calc','files','edit','fg'))
def suspend():
    out=exchange(b'\x1a');m=re.search(r'Suspended session (\d+)',out);assert m,out
    return int(m[1])
def ids():
    out=cmd('sessions')
    return [(int(m[1]),m[2],m[3],m[4]) for m in re.finditer(r'^(\d+)\s+(\S+)\s+(active|suspended)\s+(\S+)',out,re.M)]
def mem():
    out=cmd('mem');m=re.search(r'Internal heap: (\d+) free / \d+ bytes; PSRAM: (\d+) free',out);assert m,out
    return tuple(map(int,m.groups()))
def local(s):assert 'Queued' in cmd('lcd send '+json.dumps(s))
def local_key(s):assert 'Queued' in cmd('lcd key '+s)
try:
    time.sleep(1);exchange(b'\x1d\r');cmd('cd /')
    if a.cleanup_id:cmd('close '+str(a.cleanup_id))
    cmd('calc');exchange(b'123');calc=suspend()
    assert (calc,'usb-shell','suspended','calc') in ids()
    cmd('files /');files=suspend();assert len(ids())==2
    cmd('fg '+str(calc));assert '123' in exchange(b'\r')
    exchange(b'\x1d');cmd('close '+str(files));assert not ids()
    # Editor data remains unsaved in memory across shell/calculator use.
    root='/sd/_sessions_'+uuid.uuid4().hex[:10];cmd('mkdir '+root)
    report['fixture']=root
    cmd('edit '+root+'/retained.txt');exchange(b'RETAINED_EDITOR_CONTENT');editor=suspend()
    cmd('calc');exchange(b'2+3\r');exchange(b'\x1d')
    cmd('fg '+str(editor));assert 'saved' in exchange(b'\x13').lower();exchange(b'\x1d')
    assert 'RETAINED_EDITOR_CONTENT' in cmd('cat '+root+'/retained.txt')
    assert not ids()
    # Warm lifecycle memory and verify sustained resume/close recovery.
    baseline=mem()
    for i in range(15):
        cmd('calc');sid=suspend();cmd('fg');exchange(b'\x1d')
        cmd('calc');sid=suspend();assert 'Closed session' in cmd('close '+str(sid))
    assert mem()==baseline,(baseline,mem())
    report['memory_before']=baseline;report['memory_after']=mem()
    # Cross-console requests remain owned by LCD, and singleton claims persist.
    local('calc');local_key('ctrlz');records=ids()
    lcd=[r for r in records if r[1]=='lcd-shell' and r[3]=='calc'];assert len(lcd)==1,records
    lcdid=lcd[0][0];out=cmd('calc');assert 'INVALID_STATE' in out or 'busy' in out.lower() or 'already running' in out.lower(),out
    cmd('fg '+str(lcdid));assert (lcdid,'lcd-shell','active','calc') in ids()
    local_key('ctrlz')
    # Reject foreground changes while the owning shell has a running watch.
    local('watch -n 1 uptime');assert 'owner must be' in cmd('fg '+str(lcdid));local_key('ctrlc')
    cmd('files /');usb=suspend()
    conn.close();time.sleep(1)
    conn=serial.Serial(ports[0],115200,timeout=.03,write_timeout=3,exclusive=True)
    time.sleep(1);exchange(b'\r');records=ids()
    assert (lcdid,'lcd-shell','suspended','calc') in records,records
    assert all(r[1]!='usb-shell' for r in records),records
    assert 'no such app' in cmd('fg '+str(usb))
    cmd('close '+str(lcdid));assert not ids()
    assert 'no such app' in cmd('close '+str(lcdid))
    report['passed']=True
    print('PASS: calc/editor retention, multiple apps, save after resume, lifecycle memory, cross-console ownership, watch rejection, USB disconnect isolation and stale IDs')
finally:
    conn.close();a.log.write_text(json.dumps(report,indent=2)+'\n')
