#!/usr/bin/env python3
"""Background jobs/schedules acceptance on an idle board. Uses unique SD fixtures and schedule names."""
import argparse,json,re,time,uuid
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_hotplug import ANSI
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--log',type=Path,required=True)
p.add_argument('--reboot',action='store_true',help='Verify persistence across a software reboot')
p.add_argument('--cleanup-id',type=int,help='Close a known test Clock session left by a failed run')
p.add_argument('--verify-schedule',help='Verify and remove a known disabled schedule left by an interrupted reboot test')
a=p.parse_args()
report={'passed':False,'commands':[]}
def connect():
    ports=[p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x16c0,0x0483)]
    assert len(ports)==1,ports
    return serial.Serial(ports[0],115200,timeout=.03,write_timeout=3,exclusive=True)
conn=connect()
def exchange(raw=b'',prompt=True,timeout=20):
    conn.write(raw);data=bytearray();start=last=time.monotonic()
    while time.monotonic()-start<timeout:
        chunk=conn.read(16384)
        if chunk:data.extend(chunk);last=time.monotonic()
        out=ANSI.sub('',data.decode(errors='replace')).replace('\r','')
        ready=not prompt or re.search(r'[\w.-]+@[\w.-]+:/[^\n]* (?:$|\[\d+\])',out)
        if data and ready and time.monotonic()-last>.15:break
    assert 'Fault IRQ:' not in out,out
    if prompt:assert ready,out
    report['commands'].append({'input':repr(raw),'output':out})
    a.log.write_text(json.dumps(report,indent=2)+'\n');return out
def cmd(s):return exchange((s+'\r').encode())
def py(s):
    out=cmd('python -c '+json.dumps(s));assert 'Traceback' not in out,out

def write(name,data):py("f=open(%r,'w');f.write(%r);f.close()"%(root+'/'+name,data))
def start(name,slot='script'):
    out=cmd('job start '+slot+' '+root+'/'+name);assert 'job start: OK' in out,out

def memory():
    out=cmd('mem');m=re.search(r'Internal heap: (\d+) free / \d+ bytes; PSRAM: (\d+) free',out);assert m,out
    return tuple(map(int,m.groups()))
name='jt'+uuid.uuid4().hex[:8];root='/sd/_'+name;scheduled=[]
try:
    time.sleep(1);exchange(b'\x1d\r');cmd('cd /')
    if a.cleanup_id:cmd('close '+str(a.cleanup_id))
    if a.verify_schedule:
        out=cmd('schedule show '+a.verify_schedule)
        assert a.verify_schedule in out and 'disabled' in out,out
        report['recovered_persistence_check']=out
        cmd('schedule remove '+a.verify_schedule)
    initial=cmd('jobs');assert not re.search(r'script[0-3] (?:running|waiting|queued)',initial),initial
    report['existing_schedules']=cmd('schedule list')
    cmd('mkdir '+root)
    write('wait.sh','echo BEFORE\nwait 15\necho AFTER\n')
    write('short.sh','cd '+root+'\npwd\necho DONE\n')
    write('bad.sh','calc\necho NEVER\n')
    write('pipe.sh','echo bad | cat\necho NEVER\n')
    start('short.sh');time.sleep(.5);assert 'DONE' in cmd('job output script0')
    baseline=memory();report['memory_before']=baseline
    for i in range(4):start('wait.sh')
    out=cmd('jobs');assert len(re.findall(r'script[0-3] waiting',out))==4,out
    assert 'NO_MEM' in cmd('job start script '+root+'/short.sh')
    assert 'CONSOLE_OK' in cmd('echo CONSOLE_OK')
    cmd('lcd send "echo LOCAL_JOB_TEST"');assert 'LOCAL_JOB_TEST' in cmd('lcd dump')
    for i in range(4):
        assert 'job stop: OK' in cmd('job stop script'+str(i))
        out=cmd('job output script'+str(i));assert 'BEFORE' in out and 'AFTER' not in out,out
    start('short.sh');time.sleep(.5);out=cmd('job output script0');assert root in out and 'DONE' in out,out
    assert '@' in cmd('pwd') and root not in cmd('pwd')
    for bad in ('bad.sh','pipe.sh'):
        start(bad);time.sleep(.2);assert 'failed' in cmd('job status script0')
        out=cmd('job output script0');assert 'rejected' in out and 'NEVER' not in out,out
    start('missing.sh');time.sleep(.2);assert 'failed' in cmd('job status script0')
    py("f=open(%r,'w');f.write(('echo '+('a'*150)+'\\n')*80+'echo END\\n');f.close()"%(root+'/big.sh'))
    start('big.sh');time.sleep(3);out=cmd('job output script0');assert 'END' in out,out[-400:]
    assert re.search(r'dropped=[1-9]\d*',cmd('job status script0'))
    # Background jobs belong to the system, not the USB connection.
    write('disconnect.sh','wait 3\necho SURVIVED_DISCONNECT\n')
    start('disconnect.sh');conn.close();time.sleep(4);conn=connect();time.sleep(.5);exchange(b'\r')
    assert 'SURVIVED_DISCONNECT' in cmd('job output script0')
    # Measure repeated jobs after fixture creation and reconnect have warmed runtime allocations.
    baseline=memory();report['cycle_memory_before']=baseline
    for _ in range(24):start('short.sh');time.sleep(.15)
    time.sleep(.4);after=memory();report['memory_after']=after;assert after==baseline,(baseline,after)
    # Periodic entry, full pool skip, manual run, disable, persistence.
    assert 'added' in cmd('schedule add '+name+' every 1h run '+root+'/short.sh');scheduled.append(name)
    for i in range(4):start('wait.sh')
    cmd('schedule run '+name);assert 'skipped: 1' in cmd('schedule show '+name)
    for i in range(4):cmd('job stop script'+str(i))
    cmd('schedule run '+name);time.sleep(.4);assert 'DONE' in cmd('job output script0')
    cmd('schedule disable '+name)
    once=name+'x';assert 'added' in cmd('schedule add '+once+' in 1s run '+root+'/short.sh');scheduled.append(once)
    time.sleep(1.5);assert 'runs: 1' in cmd('schedule show '+once)
    cmd('schedule remove '+once);scheduled.remove(once)
    # Clock shares the scheduler and still alarms while retained.
    cmd('lcd send "clock -a 00:03"');time.sleep(.3);cmd('lcd key ctrlz');time.sleep(3)
    out=cmd('schedule list');assert '_clock' in out,out
    records=cmd('sessions');m=re.search(r'^(\d+)\s+lcd-shell\s+suspended\s+clock',records,re.M);assert m,records
    assert 'runs: 1' in cmd('schedule show _clock')
    assert 'alarm stopped' in cmd('schedule stop _clock')
    cmd('fg '+m[1]);assert 'graphics=active' in cmd('lcd');cmd('close '+m[1])
    assert '_clock' not in cmd('schedule list')
    if a.reboot:
        conn.write(b'reboot\r');conn.close();time.sleep(3)
        deadline=time.monotonic()+45
        while True:
            try:
                conn=connect();time.sleep(1);exchange(b'\r',timeout=5);break
            except (serial.SerialException,AssertionError,OSError):
                conn.close()
                if time.monotonic()>deadline:raise
                time.sleep(1)
        out=cmd('schedule show '+name);assert name in out and 'disabled' in out,out
        assert not re.search(r'script[0-3] (?:running|waiting|queued)',cmd('jobs'))
    report['fixture']=root;report['passed']=True
    print('PASS: four jobs, cooperative wait/stop, console responsiveness, isolated cwd/output, rejection paths, bounded logs, disconnect survival, heap recovery, schedule skip/run/trigger/persistence, retained Clock alarm')
finally:
    try:
        for entry in scheduled:
            try:cmd('schedule remove '+entry)
            except (serial.SerialException,AssertionError,OSError) as error:
                report.setdefault('cleanup_errors',[]).append(str(error))
    finally:
        conn.close();a.log.write_text(json.dumps(report,indent=2)+'\n')
