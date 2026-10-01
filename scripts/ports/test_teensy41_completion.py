#!/usr/bin/env python3
"""Tab completion acceptance. Exclusive USB/idle keyboard; unique SD fixtures."""
import argparse,json,re,time,uuid
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_hotplug import ANSI
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--log',type=Path,required=True);a=p.parse_args()
r={'passed':False,'commands':[]};conn=None;owned=None
root='/sd/_completion_'+uuid.uuid4().hex[:6]
def save():a.log.write_text(json.dumps(r,indent=2)+'\n')
def exchange(raw=b'',prompt=True,seconds=15):
    conn.write(raw);data=bytearray();start=last=time.monotonic();out='';ready=False
    while time.monotonic()-start<seconds:
        chunk=conn.read(16384)
        if chunk:data.extend(chunk);last=time.monotonic()
        out=ANSI.sub('',data.decode(errors='replace')).replace('\r','')
        ready=bool(re.search(r'[\w.-]+@[\w.-]+:/[^\n]* (?:$|\[\d+\])',out))
        if prompt and ready and time.monotonic()-last>.15:break
    r['commands'].append({'input':repr(raw),'output':out});save()
    assert 'Fault IRQ:' not in out,out
    if prompt:assert ready,out[-2000:]
    return out
def cmd(s):return exchange((s+'\r').encode())
def py(s):
    out=cmd('python -c '+json.dumps(s));assert 'Traceback' not in out,out

def mem():
    m=re.search(r'Internal heap: (\d+) free / \d+ bytes; PSRAM: (\d+) free',cmd('mem'));assert m;return list(map(int,m.groups()))
try:
    ports=[p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x16c0,0x0483)];assert len(ports)==1,ports
    conn=serial.Serial(ports[0],115200,timeout=.03,write_timeout=3,exclusive=True);time.sleep(.5);exchange(b'\r')
    cmd('mkdir '+root);cmd('mkdir "'+root+'/my notes"');cmd('mkdir '+root+'/projects')
    py("f=open(%r,'w');f.write('COMPLETION_FILE_OK\\n');f.close()"%(root+'/my notes/readme.txt'))
    py("for i in range(30): open(%r+('/match%%02d'%%i),'w').close()"%root)
    r['fixture']=root
    # First token and argument in the middle of a line; later arguments survive.
    out=exchange(b'ec SUFFIX\x1b[D\x1b[D\x1b[D\x1b[D\x1b[D\x1b[D\x1b[D\t\r');assert re.search(r'^SUFFIX$',out,re.M),out
    cmd('cd '+root)
    out=exchange(b'cd proj\t\r');assert root+'/projects' in cmd('pwd'),out
    cmd('cd '+root)
    out=exchange(b'cat "my no\t',False,.4);exchange(b'read\t\r');assert 'COMPLETION_FILE_OK' in r['commands'][-1]['output']
    out=exchange(b'cat my\\ notes/rea\t\r');assert 'COMPLETION_FILE_OK' in out,out
    # Replace an earlier argument while leaving the destination untouched.
    line='cp "my notes/rea" copied.txt';suffix='" copied.txt'
    exchange(line.encode(),False,.2);exchange(b'\x1b[D'*len(suffix)+b'\t\r')
    assert 'COMPLETION_FILE_OK' in cmd('cat copied.txt')
    # A capped listing, followed by normal typing after its redraw.
    exchange(b'cat mat\t',False,.3);out=exchange(b'\t',False,.4)
    assert '10 more matches' in out,out
    exchange(b'\x03',False,.2);assert 'size' in exchange(b'setterm si\t\r')
    # Complete an actual retained app ID and close it without hard-coded IDs.
    exchange(b'calc\r',False,.3);out=exchange(b'\x1a');m=re.search(r'Suspended session (\d+)',out);assert m,out;owned=int(m[1])
    exchange(b'fg \t\r',False,.3);out=exchange(b'\x1a');assert 'Suspended session '+str(owned) in out,out
    out=exchange(b'close \t\r');assert str(owned) in out,out;owned=None
    # A detached Python process appears in the JOB provider.
    exchange(b'python -c "while True: pass"\r',False,.2)
    out=exchange(b'\x1a');m=re.search(r'Suspended session (\d+)',out);assert m;owned=int(m[1])
    cmd('bg');out=exchange(b'job status \t\r');assert 'running' in out and str(owned) in out,out
    cmd('job stop '+str(owned));time.sleep(.5);owned=None
    # LCD history restores an editable line; Tab listing redraws the local shell.
    cmd('lcd send '+json.dumps('cat '+root+'/mat'));time.sleep(.2)
    cmd('lcd key up');cmd('lcd key tab');cmd('lcd key tab')
    deadline=time.monotonic()+10
    while '10 more matches' not in cmd('lcd dump'):
        assert time.monotonic()<deadline;time.sleep(.2)
    cmd('lcd key ctrlc')
    # Relative/absolute completion must release each opened directory.
    r['memory']=[]
    for i in range(5):
        out=exchange(('cat '+root+'/my\\ notes/rea\t\r').encode());assert 'COMPLETION_FILE_OK' in out
        r['memory'].append(mem())
    assert all(v==r['memory'][0] for v in r['memory']),r['memory']
    cmd('cd /');r['passed']=True
    print('PASS: command/path completion, cursor edits, quote continuation, directory traversal, bounded second-Tab listing, live session IDs, settings, exact memory recovery')
finally:
    if conn:
        if owned:
            try:cmd('close '+str(owned))
            except Exception as error:r['cleanup_error']=str(error)
        conn.close()
    save()
