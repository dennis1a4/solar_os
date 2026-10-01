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
root='/sd/_process_edges_'+uuid.uuid4().hex[:6]
try:
    time.sleep(1);exchange(b'\r');cmd('cd /');cmd('mkdir '+root);report['fixture']=root
    # Interactive REPL cancellation and retained partial expression.
    exchange(b'python\r',False,.3);out=exchange(b'12\x03',False,.3);assert 'KeyboardInterrupt' in out,out
    out=exchange(b'123\r',False,.3);assert re.search(r'^123$',out,re.M),out
    exchange(b'12',False,.2);sid=suspend();cmd('bg');exchange(('fg '+str(sid)+'\r').encode(),False,.3)
    out=exchange(b'3\r',False,.3);assert re.search(r'^123$',out,re.M),out
    exchange(b'\x04');owned.discard(sid)
    py("f=open(%r,'w');f.write('worker coexistence');f.close()"%(root+'/source.txt'))
    # Completed detached process keeps bounded output for fg/reaping.
    exchange(b'python -c "import solaros;solaros.sleep_ms(1500);print(\'X\'*12000)"\r',False,.2)
    sid=suspend();cmd('bg');time.sleep(2)
    out=cmd('job status '+str(sid));assert 'done' in out and re.search(r'dropped=[1-9]\d*',out),out
    assert 'failed' not in cmd('zip '+root+'/a.zip '+root+'/source.txt').lower()
    out=cmd('fg '+str(sid));assert 'X'*100 in out,out[-1000:];owned.discard(sid)
    assert not re.search(r'^'+str(sid)+r' python',cmd('jobs'),re.M)
    # A detached worker cannot take over the graphical display.
    exchange(b'python -c "import solaros;solaros.sleep_ms(1500);solaros.gfx.begin()"\r',False,.2)
    sid=suspend();cmd('bg');time.sleep(2)
    assert 'detached Python cannot acquire' in cmd('job output '+str(sid))
    stop(sid)
    # Foreground disconnect cancels/reaps; detached survival is in the main test.
    exchange(b'python -c "while True: pass"\r',False,.2);sid=suspend()
    exchange(('fg '+str(sid)+'\r').encode(),False,.2);conn.close();time.sleep(1);conn=connect();time.sleep(.4);exchange(b'\r')
    assert not re.search(r'^'+str(sid)+r' python',cmd('jobs'),re.M);owned.discard(sid)
    # Tail snapshots: final newline, unterminated line, empty file and bad args.
    path=root+'/tail.txt'
    for data,expected in [('a\nb\nc\n','b\nc\n'),('a\nb\nc','b\nc'),('','')]:
        py("f=open(%r,'w');f.write(%r);f.close()"%(path,data))
        out=cmd('tail -n 2 '+path);body=out.split('\n',1)[1].split('user@')[0]
        assert body==expected,(body,expected)
    assert 'usage:' in cmd('tail -n 0 '+path)
    report['passed']=True;print('PASS: REPL Ctrl+C and retained expression, done-job reattachment, bounded output, detached graphics rejection, foreground disconnect cleanup, tail boundaries')
finally:
    try:
        for sid in list(owned):
            try:cmd('job stop '+str(sid))
            except Exception as error:report.setdefault('cleanup_errors',[]).append(str(error))
    finally:conn.close();save()
