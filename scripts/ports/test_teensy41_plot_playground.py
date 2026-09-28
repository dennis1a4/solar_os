#!/usr/bin/env python3
"""Hardware Plot/Playground checks; leave the local keyboard idle.
Downloads the official catalog and installs the small hello-python example.
Keeps that example/catalog for manual testing; removes its temporary CSV.
"""
import argparse,json,re,time,uuid
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_shell import ANSI
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--log',type=Path,required=True)
group=p.add_mutually_exclusive_group()
group.add_argument('--plot-only',action='store_true')
group.add_argument('--playground-only',action='store_true')
a=p.parse_args()
report={'passed':False,'commands':[]}
ports=[x.device for x in list_ports.comports() if (x.vid,x.pid)==(0x16c0,0x0483)]
assert len(ports)==1,ports
conn=serial.Serial(ports[0],115200,timeout=.03,write_timeout=3,exclusive=True)
prompt=re.compile(r'[\w.-]+@[\w.-]+:/[^\n]* $')
def exchange(raw=b'',shell=True,timeout=25):
    conn.write(raw);data=bytearray();deadline=time.monotonic()+timeout;last=time.monotonic()
    while time.monotonic()<deadline:
        b=conn.read(16384)
        if b:data.extend(b);last=time.monotonic()
        text=ANSI.sub('',data.decode(errors='replace')).replace('\r','')
        assert 'Fault IRQ:' not in text,text
        if (shell and prompt.search(text)) or (not shell and time.monotonic()-last>.6):
            report['commands'].append({'input':repr(raw),'output':text})
            a.log.write_text(json.dumps(report,indent=2)+'\n');return text
    raise RuntimeError(f'timeout {raw!r}: {data[-2500:]!r}')
def cmd(s,timeout=25):return exchange((s+'\r').encode(),timeout=timeout)
def local(s):
    assert 'Queued' in cmd('lcd send '+json.dumps(s));time.sleep(.5)
def dump():return cmd('lcd dump')
def status(active):
    out=cmd('lcd');assert f'graphics={active}' in out,out
    return int(re.search(r'frames=(\d+)',out).group(1))
def mem():
    out=cmd('mem');m=re.search(r'Internal heap: (\d+) free / \d+ bytes; PSRAM: (\d+) free',out);assert m,out
    return tuple(map(int,m.groups()))
path='/sd/plot-test-'+uuid.uuid4().hex[:8]+'.csv'
report['temporary_csv']=path
created=False
try:
    time.sleep(1);exchange(b'\x1d\r');cmd('lcd key exit');time.sleep(.3)
    apps=cmd('apps');assert 'plot -' in apps and 'playground -' in apps,apps
    if not a.playground_only:
        assert 'display' in cmd('plot uptime')
        local('plot -f /sd/no-such-plot-test.csv');status('idle')
        csv='uptime_ms,value\n'+''.join(f'{i*100},{(i%20)-10}\n' for i in range(100))
        code=f"open({path!r},'w').write({csv!r})"
        # Keep the command within the shell line bound by generating rows on-device.
        code=f"open({path!r},'w').write('uptime_ms,value\\n'+''.join(str(i*100)+','+str((i%20)-10)+'\\n' for i in range(100)))"
        result=cmd('python -c '+json.dumps(code));assert 'Traceback' not in result,result
        created=True
        # Warm up this console's libc file/float formatting state before leak checks.
        local('plot -f '+path);status('active');cmd('lcd key exit');time.sleep(.3)
        baseline=mem();frames=status('idle')
        for _ in range(3):
            local('plot -f '+path);new=status('active');assert new>frames;frames=new
            assert 'USB_DURING_PLOT' in cmd('echo USB_DURING_PLOT')
            cmd('lcd key exit');time.sleep(.3);status('idle')
        assert mem()==baseline,(baseline,mem())
        local('plot uptime --rate 250');first=status('active');time.sleep(2);assert status('active')>first
        cmd('lcd key exit');time.sleep(.3);status('idle')
        cmd('rm '+path);created=False;report['plot_passed']=True
    if not a.plot_only:
        assert 'raw.githubusercontent.com' in cmd('playground source')
        source='https://raw.githubusercontent.com/nilseuropa/solar_os_playground/main/dist/catalog.json'
        cmd('playground source '+source);assert source in cmd('playground source')
        cmd('playground source reset');cmd('rtc set '+str(int(time.time())));cmd('network up',timeout=30)
        for _ in range(20):
            net=cmd('network status')
            if 'dhcp=bound' in net.lower() or ('192.168.' in net or '10.' in net):break
            time.sleep(.5)
        local('playground refresh')
        deadline=time.monotonic()+75
        while time.monotonic()<deadline:
            text=dump()
            if 'catalog refreshed' in text.lower() or 'Hello Python' in text:break
            if 'failed:' in text.lower() or 'operation failed' in text.lower():raise AssertionError(text)
            time.sleep(1)
        else:raise AssertionError(text)
        cmd('lcd key exit');time.sleep(.5)
        search=cmd('playground search hello');assert 'hello-python' in search and 'hello-lua' in search,search
        install=cmd('playground install hello-python',timeout=75);assert 'installed hello-python' in install.lower() or 'done' in install.lower(),install
        run=cmd('playground run hello-python');assert 'Hello from the SolarOS Playground!' in run,run
        alias=cmd('hello-python');assert 'Hello from the SolarOS Playground!' in alias,alias
        assert 'unavailable' in cmd('playground install hello-lua')
        assert 'unavailable' in cmd('playground install mandelbrot-python')
        local('playground');time.sleep(1);assert 'Playground' in dump()
        cmd('lcd key exit');time.sleep(.5)
        report['playground_passed']=True
    report['passed']=True
    print('PASS:',{k:v for k,v in report.items() if k!='commands'})
finally:
    if created:
        try:cmd('lcd key exit');cmd('rm '+path)
        except Exception as exc:report['cleanup_error']=str(exc)
    conn.close();a.log.write_text(json.dumps(report,indent=2)+'\n')
