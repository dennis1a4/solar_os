#!/usr/bin/env python3
"""RAMFS acceptance: exclusive USB, idle keyboard; unique temporary SD fixture."""
import argparse,json,re,time,uuid
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_hotplug import ANSI
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--log',type=Path,required=True);p.add_argument('--edges-only',action='store_true');p.add_argument('--reboot',action='store_true');a=p.parse_args()
r={'passed':False,'commands':[]};conn=None;sid=None;mounted=False
name='/rt'+uuid.uuid4().hex[:6];sd='/sd/_ramfs_'+uuid.uuid4().hex[:6]+'.txt'
def save():a.log.write_text(json.dumps(r,indent=2)+'\n')
def exchange(raw=b'',prompt=True,seconds=20):
    conn.write(raw);data=bytearray();start=last=time.monotonic();out='';ready=False
    while time.monotonic()-start<seconds:
        chunk=conn.read(16384)
        if chunk:data.extend(chunk);last=time.monotonic()
        out=ANSI.sub('',data.decode(errors='replace')).replace('\r','')
        ready=bool(re.search(r'[\w.-]+@[\w.-]+:/[^\n]* (?:$|\[\d+\])',out))
        if prompt and ready and time.monotonic()-last>.15:break
    r['commands'].append({'input':repr(raw),'output':out});save()
    assert 'Fault IRQ:' not in out,out
    if prompt:assert ready,out[-1500:]
    return out
def cmd(s,seconds=20):return exchange((s+'\r').encode(),seconds=seconds)
def py(s):
    command='python -c '+json.dumps(s);assert len(command)<191,command
    out=cmd(command);assert not any(x in out for x in ('Traceback','SyntaxError','too long')),out
    return out
def memory():
    m=re.search(r'Internal heap: (\d+) free / \d+ bytes; PSRAM: (\d+) free',cmd('mem'));assert m;return list(map(int,m.groups()))
try:
    ports=[q.device for q in list_ports.comports() if q.vid==0x16c0 and q.pid==0x0483];assert len(ports)==1,ports
    conn=serial.Serial(ports[0],115200,timeout=.05,exclusive=True);time.sleep(.5);exchange(b'\r');cmd('cd /')
    assert 'no mounts' in cmd('ramfs'), 'Test requires no pre-existing RAMFS mounts'
    if not a.edges_only:
        for path in ('/','/sd','/flash','/usb','/a/b','/..'):
            assert 'INVALID_ARG' in cmd('ramfs mount '+path+' 64k')
        assert 'size must' in cmd('ramfs mount '+name+' 999999999999999999999m')
        assert 'ramfs: OK' in cmd('ramfs mount '+name+' 64k');mounted=True
        assert name[1:] in cmd('ls /')
        py("f=open(%r,'w');f.write('RAMFS_OK\\n');f.close()"%(name+'/hello.txt'))
        assert 'RAMFS_OK' in cmd('cat '+name+'/hello.txt')
        assert 'hello.txt' in cmd('ls '+name)
        cmd('cp '+name+'/hello.txt '+sd);assert 'RAMFS_OK' in cmd('cat '+sd)
        cmd('mv '+sd+' '+name+'/moved.txt');assert 'RAMFS_OK' in cmd('cat '+name+'/moved.txt')
        cmd('mkdir '+name+'/sub');cmd('mv '+name+'/moved.txt '+name+'/sub/moved.txt')
        assert 'RAMFS_OK' in cmd('tail -n 1 '+name+'/sub/moved.txt')
        py("f=open(%r,'a');f.write('APPEND\\n');f.close()"%(name+'/hello.txt'))
        assert 'APPEND' in cmd('cat '+name+'/hello.txt')
        py("f=open(%r,'w');f.write('print(12345)');f.close()"%(name+'/run.py'))
        assert re.search(r'^12345$',cmd('python '+name+'/run.py'),re.M)
        assert 'failed' not in cmd('zip '+name+'/test.zip '+name+'/hello.txt').lower()
        assert 'hello.txt' in cmd('unzip -l '+name+'/test.zip')
        assert name in cmd('df',seconds=100)
        # Completion enumerates only this mount's directory.
        assert 'RAMFS_OK' in exchange(('cat '+name+'/hel\t\r').encode())
        assert 'volatile PSRAM' in exchange(b'ramfs sta\t\r')
        out=exchange(('files '+name+'\r').encode(),False,.6);assert 'hello.txt' in out,out[-1500:];exchange(b'\x1d')
        # A retained Python worker keeps its open file; unmount cannot free its arena.
        code="f=open(%r);input('HOLD:');f.close()"%(name+'/hello.txt')
        out=exchange(('python -c '+json.dumps(code)+'\r').encode(),False,.4);assert 'HOLD:' in out
        out=exchange(b'\x1a');m=re.search(r'Suspended session (\d+)',out);assert m,out;sid=int(m[1])
        assert 'INVALID_STATE' in cmd('ramfs unmount '+name)
        cmd('bg');assert 'INVALID_STATE' in cmd('ramfs unmount '+name)
        cmd('job stop '+str(sid));sid=None
        assert 'ramfs: OK' in cmd('ramfs unmount '+name);mounted=False
        # No fallback to a similarly named SD path after unmount.
        assert 'RAMFS_OK' not in cmd('cat '+name+'/hello.txt')
        # ENOSPC must preserve already-written bytes; execute code from the command
        # line so no script file consumes the deliberately tiny RAMFS quota.
        assert 'ramfs: OK' in cmd('ramfs mount '+name+' 1k');mounted=True
        py("f=open(%r,'w');f.write('SAFE');f.close()"%(name+'/small'))
        out=cmd('python -c '+json.dumps("f=open(%r,'a');f.write('X'*2048);f.close()"%(name+'/small')))
        assert 'OSError' in out,out
        assert 'SAFE' in cmd('cat '+name+'/small')
        assert 'ramfs: OK' in cmd('ramfs unmount '+name);mounted=False
        py("print('WARM')");r['cycles']=[]
        for i in range(5):
            assert 'ramfs: OK' in cmd('ramfs mount '+name+' 64k');mounted=True
            py("f=open(%r,'w');f.write('cycle');f.close()"%(name+'/file'))
            cmd('ls '+name);assert 'ramfs: OK' in cmd('ramfs unmount '+name);mounted=False
            r['cycles'].append(memory())
        assert all(v==r['cycles'][0] for v in r['cycles']),r['cycles']
    # Editor atomic save, shared LCD visibility, multiple mounts and cold reboot.
    assert 'ramfs: OK' in cmd('ramfs mount '+name+' 64k');mounted=True
    exchange(('edit '+name+'/edited.py\r').encode(),False,.4)
    exchange(b'print(54321)\r',False,.2)
    assert 'saved' in exchange(b'\x13',False,.4).lower()
    exchange(b'\x1d')
    assert re.search(r'^54321$',cmd('python '+name+'/edited.py'),re.M)
    exchange(('edit '+name+'/edited.py\r').encode(),False,.3)
    assert 'saved' in exchange(b'\x01print(65432)\r\x13',False,.4).lower()
    exchange(b'\x1d');assert '65432' in cmd('python '+name+'/edited.py')
    cmd('lcd send '+json.dumps('ls '+name));time.sleep(.3)
    assert 'edited.py' in cmd('lcd dump')
    for suffix in ('a','b','c'):assert 'ramfs: OK' in cmd('ramfs mount '+name+suffix+' 64k')
    assert 'NO_MEM' in cmd('ramfs mount '+name+'d 64k')
    for suffix in ('a','b','c'):assert 'ramfs: OK' in cmd('ramfs unmount '+name+suffix)
    # Confirm ordinary flash access remains functional after extending the router.
    flash='/flash/_ramfs_'+uuid.uuid4().hex[:6]+'.txt'
    cmd('cp '+name+'/edited.py '+flash)
    assert '65432' in cmd('cat '+flash);cmd('rm '+flash)
    if a.reboot:
        r['reboot_mount']=name;save();conn.write(b'reboot\r');time.sleep(.5);conn.close();conn=None
        time.sleep(3)
        deadline=time.monotonic()+15
        while time.monotonic()<deadline:
            try:
                ports=[q.device for q in list_ports.comports() if q.vid==0x16c0 and q.pid==0x0483]
                if len(ports)==1:
                    conn=serial.Serial(ports[0],115200,timeout=.05,exclusive=True)
                    time.sleep(.5);conn.write(b'\r');break
            except (serial.SerialException,OSError):
                if conn:conn.close();conn=None
            time.sleep(.2)
        assert conn,'Teensy did not reconnect';time.sleep(1);exchange(b'\r')
        assert 'no mounts' in cmd('ramfs');mounted=False
        assert name[1:] not in cmd('ls /')
        r['reboot_volatile']=True
    else:
        assert 'ramfs: OK' in cmd('ramfs unmount '+name);mounted=False
    assert 'no mounts' in cmd('ramfs');r['passed']=True
    print('PASS: RAMFS acceptance including editor save, multiple mounts, LCD visibility and flash routing')
finally:
    if conn:
        try:
            if sid:cmd('job stop '+str(sid))
            cmd('cd /')
            for suffix in ('a','b','c'):cmd('ramfs unmount '+name+suffix)
            if mounted:cmd('ramfs unmount '+name)
            cmd('rm '+sd)
        except Exception as e:r['cleanup_error']=str(e)
        conn.close()
    save()
