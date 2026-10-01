#!/usr/bin/env python3
"""Syntax editor acceptance: exclusive USB, idle consoles; volatile RAMFS fixture."""
import argparse,json,re,time
from pathlib import Path
import serial,pyte
from serial.tools import list_ports
from test_teensy41_hotplug import ANSI
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--log',type=Path,required=True);a=p.parse_args()
r={'passed':False,'commands':[]};conn=None;sid=None;mounted=False;editing=False
screen=pyte.Screen(96,64);stream=pyte.Stream(screen)
def save():a.log.write_text(json.dumps(r,indent=2)+'\n')
def exchange(raw=b'',prompt=True,seconds=20):
    conn.write(raw);data=bytearray();start=last=time.monotonic();out='';ready=False
    while time.monotonic()-start<seconds:
        chunk=conn.read(32768)
        if chunk:data.extend(chunk);last=time.monotonic();stream.feed(chunk.decode(errors='replace'))
        out=ANSI.sub('',data.decode(errors='replace')).replace('\r','')
        ready=bool(re.search(r'[\w.-]+@[\w.-]+:/[^\n]* (?:$|\[\d+\])',out))
        if prompt and ready and time.monotonic()-last>.15:break
    r['commands'].append({'input':repr(raw),'output':data.decode(errors='replace')});save()
    assert 'Fault IRQ:' not in out,out
    if prompt:assert ready,out[-1200:]
    return out
def cmd(s):return exchange((s+'\r').encode())
def py(s):
    command='python -c '+json.dumps(s);assert len(command)<191
    out=cmd(command);assert 'Traceback' not in out,out
    return out
def color(row,word,expected):
    line=screen.display[row];start=line.index(word)
    found=[screen.buffer[row][i].fg for i in range(start,start+len(word))]
    assert all(c==expected for c in found),(row,word,found,expected,line)
def mem():
    m=re.search(r'Internal heap: (\d+) free / \d+ bytes; PSRAM: (\d+) free',cmd('mem'));assert m;return list(map(int,m.groups()))
try:
    ports=[q.device for q in list_ports.comports() if q.vid==0x16c0 and q.pid==0x0483];assert len(ports)==1,ports
    conn=serial.Serial(ports[0],115200,timeout=.05,exclusive=True);time.sleep(.5);exchange(b'\r');cmd('cd /')
    assert 'ramfs: OK' in cmd('ramfs mount /syntax-test 256k');mounted=True
    lines=['# comment','class Sensor:','    def read(self):','        print(len(range(3)))','        return True',
           'text = """open','inside','"""','x = 0xff + .5e-2','label = r"hello"']
    for i,line in enumerate(lines):py("f=open('/syntax-test/demo.py',%r);f.write(%r);f.close()"%('w' if i==0 else 'a',line+'\n'))
    exchange(b'edit /syntax-test/demo.py\r',False,.8);editing=True
    color(1,'# comment','brightblack');color(2,'class','brightblue');color(2,'Sensor','brightbrown')
    color(3,'read','brightbrown');color(4,'print','brightmagenta');color(4,'3','brightcyan')
    color(5,'True','brightwhite');color(7,'inside','brightgreen');color(9,'.5e-2','brightcyan');color(10,'r"hello"','brightgreen')
    # Selection uses default foreground and reverse video, then arrow clears it.
    exchange(b'\x01',False,.3)
    assert screen.buffer[2][0].reverse and screen.buffer[2][0].fg=='default'
    exchange(b'\x1b[C',False,.3)
    out=exchange(b'\x1a');editing=False;m=re.search(r'Suspended session (\d+)',out);assert m;sid=int(m[1])
    exchange(('fg '+str(sid)+'\r').encode(),False,.5);editing=True;sid=None;color(2,'class','brightblue')
    exchange(b'\x1d');editing=False
    assert screen.cursor.attrs.fg=='default',screen.cursor.attrs
    # Fresh editor starts at byte zero. Inserting an opening triple quote changes
    # the next class line to a string; backspaces restore the original state.
    exchange(b'edit /syntax-test/demo.py\r',False,.4);editing=True
    exchange(b'"""',False,.5);color(2,'class','brightgreen')
    exchange(b'\x7f\x7f\x7f',False,.5);color(2,'class','brightblue')
    exchange(b'\x13',False,.3);exchange(b'\x1d');editing=False
    r['memory']=[]
    for i in range(5):
        exchange(b'edit /syntax-test/demo.py\r',False,.2);editing=True
        exchange(b'\x1d');editing=False;r['memory'].append(mem())
    assert all(v==r['memory'][0] for v in r['memory']),r['memory']
    cmd('lcd send "edit /syntax-test/demo.py"');time.sleep(.5);assert 'class Sensor' in cmd('lcd dump')
    cmd('lcd key exit');time.sleep(.2)
    r['passed']=True;print('PASS: token colors, selection contrast, multiline edit propagation, resume, shell color restoration, LCD rendering and exact cleanup')
finally:
    if conn:
        try:
            if editing:exchange(b'\x11')
            if sid:cmd('close '+str(sid))
            cmd('lcd key exit')
            if mounted:cmd('ramfs unmount /syntax-test')
        except Exception as e:r['cleanup_error']=str(e)
        conn.close()
    save()
