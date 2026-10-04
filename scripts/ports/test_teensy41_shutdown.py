#!/usr/bin/env python3
"""Exclusive USB shutdown acceptance. Uses --check; never cuts board power.
Creates uniquely named SD fixtures, stops its own Python/script/serial jobs,
and uses the LCD console to check refusal with an active app. Start idle.
"""
import argparse,json,re,time,uuid
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_hotplug import ANSI
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--log',type=Path,required=True)
a=p.parse_args();report={'passed':False,'commands':[]}
ports=[p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x16c0,0x0483)]
assert len(ports)==1,ports
conn=serial.Serial(ports[0],115200,timeout=.03,write_timeout=3,exclusive=True)
root='/sd/_shutdown_'+uuid.uuid4().hex[:6]
owned=None

def exchange(raw=b'',prompt=True,seconds=22):
    conn.write(raw);data=bytearray();start=last=time.monotonic();found=False
    while time.monotonic()-start<seconds:
        chunk=conn.read(16384)
        if chunk:data.extend(chunk);last=time.monotonic()
        out=ANSI.sub('',data.decode(errors='replace')).replace('\r','')
        found=bool(re.search(r'[\w.-]+@[\w.-]+:/[^\n]* (?:$|\[\d+\])',out,re.M))
        if prompt and found and time.monotonic()-last>.2:break
    report['commands'].append({'input':repr(raw),'output':out});a.log.write_text(json.dumps(report,indent=2)+'\n')
    assert 'Fault IRQ:' not in out,out
    if prompt:assert found,out[-2000:]
    return out

def cmd(s):return exchange((s+'\r').encode())
def py(s):
    command='python -c '+json.dumps(s);assert len(command)<=191,command
    out=cmd(command);assert 'Traceback' not in out and 'SyntaxError' not in out,out
    return out

def write_file(name,contents):
    path=root+'/'+name
    py("f=open(%r,'w');f.close()"%path)
    for pos in range(0,len(contents),40):
        py("f=open(%r,'a');f.write(%r);f.close()"%(path,contents[pos:pos+40]))
    return path

def start_script(name,contents,detach=True):
    global owned
    path=write_file(name,contents)
    exchange(('python '+path+'\r').encode(),False,.4)
    out=exchange(b'\x1a');m=re.search(r'Suspended session (\d+)',out);assert m,out
    owned=int(m[1])
    if detach:cmd('bg '+str(owned))

def check(expected='check complete'):
    assert 'Shutdown requested' in cmd('poweroff --check')
    out=cmd('poweroff status');assert expected in out,out
    return out

def memory():
    m=re.search(r'Internal heap: (\d+) free / \d+ bytes; PSRAM: (\d+) free',cmd('mem'));assert m
    return tuple(map(int,m.groups()))

try:
    time.sleep(.5);exchange(b'\r');cmd('cd /')
    assert not re.search(r'^\d+ python',cmd('jobs'),re.M)
    assert not re.search(r'^\d+\s+\S+\s+(?:active|suspended)',cmd('sessions'),re.M)
    assert all(x in cmd('serial status') for x in ['uart7: closed','uart8: closed','uart3: closed'])
    assert 'False' in py('import solaros;print(solaros.shutdown_requested())')
    baseline=memory();report['memory_before']=baseline
    cmd('mkdir '+root)
    check();assert memory()==baseline
    # Do not silently discard even an otherwise harmless retained/active app.
    cmd('lcd send "calc"');time.sleep(.3)
    check('blocked: close')
    assert 'calc' in cmd('sessions')
    cmd('lcd key exit');time.sleep(.3)
    # Cooperative cancellation closes a file without an interrupt.
    start_script('coop.py',"import solaros,time\nf=open(%r,'w')\ntry:\n while not solaros.shutdown_requested(): time.sleep_ms(5)\n f.write('COOPERATIVE')\nfinally:\n f.close()\n"%(root+'/coop.txt'))
    check();owned=None;assert 'COOPERATIVE' in cmd('cat '+root+'/coop.txt')
    assert memory()==baseline
    # A suspended CPU loop is resumed; one interrupt allows a long finally.
    start_script('final.py',"import time\nf=open(%r,'w')\ntry:\n while True: pass\nfinally:\n time.sleep_ms(3000)\n f.write('FINALLY_COMPLETE')\n f.close()\n"%(root+'/final.txt'),False)
    check();owned=None;assert 'FINALLY_COMPLETE' in cmd('cat '+root+'/final.txt')
    assert memory()==baseline
    # Catching the interrupt must keep power on until the 15-second deadline.
    start_script('refuse.py',"import solaros,time\nwhile not solaros.shutdown_requested(): time.sleep_ms(5)\ntry:\n while True: pass\nexcept KeyboardInterrupt:\n while solaros.shutdown_requested(): time.sleep_ms(5)\nf=open(%r,'w');f.write('SURVIVED');f.close()\n"%(root+'/refuse.txt'))
    check('timeout');time.sleep(.5)
    # A timed-out worker can finish normally after the flag clears; reap it.
    cmd('job stop '+str(owned));owned=None
    assert 'SURVIVED' in cmd('cat '+root+'/refuse.txt')
    assert memory()==baseline
    # Background script input and serial log handles must be closed/drained.
    path=write_file('wait.sh','wait 60000\necho SHOULD_NOT_RUN\n')
    cmd('job start script0 '+path)
    assert 'serial: OK' in cmd('serial record uart8 9600 '+root+'/uart.bin')
    check();assert 'uart8: closed' in cmd('serial status')
    assert 'script0 stopped' in cmd('jobs');assert memory()==baseline
    assert 'SHOULD_NOT_RUN' not in cmd('job output script0')
    # A foreground REPL on the other console also leaves without forced deletion.
    cmd('lcd send \"python\"');time.sleep(.3)
    check();assert 'python' not in cmd('sessions')
    assert memory()==baseline
    assert 'poweroff' in cmd('commands')
    out=exchange(b'man poweroff\r',False,.6)
    assert 'poweroff [--check|status]' in out,out
    exchange(b'\x1d')
    report['memory_after']=memory();report['fixtures']=root
    report['passed']=True
    print('PASS: idle/blocked shutdown, cooperative Python, suspended Python finally, interrupt refusal timeout, script and serial cleanup, memory recovery')
finally:
    try:
        if owned:cmd('job stop '+str(owned))
    finally:
        conn.close();a.log.write_text(json.dumps(report,indent=2)+'\n')
