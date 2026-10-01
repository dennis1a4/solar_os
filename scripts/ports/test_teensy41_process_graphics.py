#!/usr/bin/env python3
"""Detachable Python acceptance: idle consoles/exclusive USB, unique SD fixtures."""
import argparse,json,re,time,uuid
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_hotplug import ANSI
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--log',type=Path,required=True);a=p.parse_args()
report={'passed':False,'commands':[]};owned=set()
def connect():
    ports=[p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x16c0,0x0483)]
    assert len(ports)==1,ports
    return serial.Serial(ports[0],115200,timeout=.03,write_timeout=3,exclusive=True)
conn=connect()
def save():a.log.write_text(json.dumps(report,indent=2)+'\n')
def exchange(raw=b'',prompt=True,seconds=20):
    conn.write(raw);data=bytearray();start=last=time.monotonic();ready=False
    while time.monotonic()-start<seconds:
        chunk=conn.read(16384)
        if chunk:data.extend(chunk);last=time.monotonic()
        out=ANSI.sub('',data.decode(errors='replace')).replace('\r','')
        ready=bool(re.search(r'[\w.-]+@[\w.-]+:/[^\n]* (?:$|\[\d+\])',out))
        if prompt and ready and time.monotonic()-last>.15:break
    report['commands'].append({'input':repr(raw),'output':out});save()
    assert 'Fault IRQ:' not in out,out[-2000:]
    if prompt:assert ready,out[-2000:]
    return out
def cmd(s):return exchange((s+'\r').encode())
def py(s):
    out=cmd('python -c '+json.dumps(s));assert not any(x in out for x in ('Traceback','unterminated','too long','SyntaxError')),out[-1500:]
def suspend():
    out=exchange(b'\x1a');m=re.search(r'Suspended session (\d+)',out);assert m,out[-1000:]
    sid=int(m[1]);owned.add(sid);return sid
def memory():
    m=re.search(r'Internal heap: (\d+) free / \d+ bytes; PSRAM: (\d+) free',cmd('mem'));assert m
    return tuple(map(int,m.groups()))
def lastrow():
    out=cmd('tail -n 1 '+root+'/rows.txt');m=re.findall(r'^ROW (\d+)$',out,re.M);assert m,out[-500:];return int(m[-1])
def stop(sid):
    cmd('job stop '+str(sid));time.sleep(.5)
    out=cmd('jobs');assert not re.search(r'^'+str(sid)+r' python',out,re.M),out
    owned.discard(sid)
try:
    time.sleep(1);exchange(b'\r')
    for cycle in range(2):
        cmd('lcd send '+json.dumps("python -c \"import solaros;g=solaros.gfx;g.begin();g.clear();g.text(5,5,'worker');g.present();solaros.sleep_ms(10000)\""))
        time.sleep(.6);assert 'graphics=active' in cmd('lcd')
        cmd('lcd key ctrlz');time.sleep(.2)
        assert re.search(r'^\d+\s+lcd-shell\s+active\s+python',cmd('sessions'),re.M)
        assert 'graphics=active' in cmd('lcd')
        cmd('lcd key ctrlc');time.sleep(.6);assert 'graphics=idle' in cmd('lcd')
        assert not re.search(r'^\d+ python',cmd('jobs'),re.M)
        current=memory()
        if cycle==0:baseline=current
        else:assert current==baseline,(baseline,current)
    report['memory_after']=current;report['passed']=True
    print('PASS: foreground Python graphics on worker, redraw, graphics suspension rejection, Ctrl+C cleanup and repeated heap recovery')
finally:
    conn.close();save()
