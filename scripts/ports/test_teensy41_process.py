#!/usr/bin/env python3
"""Detachable Python acceptance: idle consoles/exclusive USB, unique SD fixtures."""
import argparse,json,re,time,uuid
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_hotplug import ANSI
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--log',type=Path,required=True);p.add_argument('--cleanup-only',type=int,help='Stop a known process left by an interrupted test');a=p.parse_args()
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
if a.cleanup_only:
    time.sleep(.5);exchange(b'\x1d\r');stop(a.cleanup_only);conn.close();save();raise SystemExit(0)
root='/sd/_process_'+uuid.uuid4().hex[:8]
try:
    time.sleep(1);exchange(b'\r');cmd('cd /');report['initial_jobs']=cmd('jobs')
    assert not re.search(r'^\d+ python',report['initial_jobs'],re.M),report['initial_jobs']
    cmd('mkdir '+root);report['fixture']=root
    code="f=open(%r,'w')\ni=0\nwhile True:\n f.write('ROW '+str(i)+'\\n')\n f.flush()\n print('ROW',i)\n i+=1\n"%(root+'/rows.txt')
    for i,line in enumerate(code.splitlines(True)):
        py("f=open(%r,%r);f.write(%r);f.close()"%(root+'/logger.py','w' if i==0 else 'a',line))
    py("print('WARM_VM')");base=memory();report['memory_before']=base
    out=exchange(('python '+root+'/logger.py\r').encode(),False,.7);assert 'ROW' in out,out[-1000:]
    sid=suspend();one=lastrow();time.sleep(.4);assert lastrow()==one
    assert 'suspended' in cmd('jobs');assert 'background' in cmd('bg')
    time.sleep(.6);assert lastrow()>one
    assert 'ROW' in cmd('job output '+str(sid))
    # The VM singleton remains claimed while detached.
    assert 'already running' in cmd('python -c "print(1)"').lower()
    exchange(b'calc\r',False,.7);out=exchange(b'123+4\r',False,1.2);assert '127' in out,out[-1000:]
    exchange(b'\x1d');two=lastrow();assert two>one
    exchange(('fg '+str(sid)+'\r').encode(),False,.4);assert suspend()==sid
    cmd('bg');before=lastrow();conn.close();time.sleep(.7);conn=connect();time.sleep(.5);exchange(b'\r');assert lastrow()>before
    # Reattach on LCD, suspend there, detach again, then return to USB.
    cmd('lcd send '+json.dumps('fg '+str(sid)));time.sleep(.3)
    assert re.search(r'^'+str(sid)+r'\s+lcd-shell\s+active\s+python',cmd('sessions'),re.M)
    cmd('lcd key ctrlz');cmd('lcd send '+json.dumps('bg '+str(sid)));time.sleep(.3)
    exchange(('fg '+str(sid)+'\r').encode(),False,.3);assert suspend()==sid;cmd('bg');stop(sid)
    py("f=open(%r,'a');f.write('CLOSED_OK\\n');f.close()"%(root+'/rows.txt'))
    assert 'CLOSED_OK' in cmd('tail -n 1 '+root+'/rows.txt')
    # input() retains partial input and never reads detached shell commands.
    exchange(b'python -c "s=input(\'ASK:\');print(\'GOT:\'+s)"\r',False,.3)
    exchange(b'abc',False,.2);sid=suspend();cmd('bg');assert 'waiting-input' in cmd('jobs')
    assert 'SHELL_ONLY' in cmd('echo SHELL_ONLY')
    exchange(('fg '+str(sid)+'\r').encode(),False,.3)
    out=exchange(b'def\r');assert 'GOT:abcdef' in out,out[-1500:];owned.discard(sid)
    # CPU-bound cancellation and repeated VM cleanup.
    base=memory();report['cycle_memory_before']=base
    for i in range(8):
        exchange(b'python -c "while True: pass"\r',False,.15);sid=suspend();cmd('bg');stop(sid)
        assert memory()==base,(i,base,memory())
    # Stop a suspended worker without first resuming it.
    exchange(b'python -c "while True: pass"\r',False,.15);sid=suspend()
    cmd('close '+str(sid));time.sleep(.4);assert not re.search(r'^'+str(sid)+r' python',cmd('jobs'),re.M);owned.discard(sid)
    after=memory();report['memory_after']=after;assert after==base,(base,after)
    report['passed']=True;print('PASS: Python safe pause/bg/fg, file logger with tail and Calc, singleton admission, USB disconnect survival, cross-console reattach, retained input, cooperative stop, repeated VM cleanup')
finally:
    try:
        if owned:exchange(b'\x1d',seconds=5)
        for sid in list(owned):
            try:cmd('job stop '+str(sid))
            except Exception as error:report.setdefault('cleanup_errors',[]).append(str(error))
    finally:conn.close();save()
