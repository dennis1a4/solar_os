#!/usr/bin/env python3
"""Stage 3 acceptance against local TCP/NTP fixtures; sets RTC to host UTC.
Requires exclusive USB and idle keyboard. Leaves Ethernet up, timezone unchanged.
Scans only this test host and explicitly selected fixture ports.
"""
import argparse,json,re,socket,struct,threading,time
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_hotplug import ANSI
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--log',type=Path,required=True);a=p.parse_args()
r={'passed':False,'commands':[]};conn=None;servers=[];stop=threading.Event();mode='valid';lcd_owned=False
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
def mem():
    m=re.search(r'Internal heap: (\d+) free / \d+ bytes; PSRAM: (\d+) free',cmd('mem'));assert m
    return list(map(int,m.groups()))
def rtc():
    m=re.search(r'epoch=(\d+)',cmd('rtc'));assert m;return int(m[1])
def tcp_worker(sock):
    while not stop.is_set():
        try:
            c,_=sock.accept()
            # Model a service waiting for a request; keep it open until the probe closes.
            c.settimeout(2)
            try:c.recv(1)
            except socket.timeout:pass
            finally:c.close()
        except socket.timeout:pass
        except OSError:return

def stamp(epoch):
    seconds=int(epoch);return struct.pack('!II',(seconds+2208988800)&0xffffffff,int((epoch-seconds)*2**32))
def ntp_worker(sock):
    while not stop.is_set():
        try:req,peer=sock.recvfrom(1024)
        except socket.timeout:continue
        except OSError:return
        if len(req)!=48 or mode=='drop':continue
        reply=bytearray(48);reply[0]=0x24;reply[1]=2;reply[24:32]=req[40:48]
        now=time.time() if mode!='era' else 2085978500
        reply[32:40]=reply[40:48]=stamp(now)
        if mode=='denied':reply[1]=0
        if mode in ('bad','mixed'):
            wrong=reply.copy();wrong[24]^=1;sock.sendto(wrong,peer)
            sock.sendto(reply[:20],peer)
            wrong=reply.copy();wrong[0]|=0xc0;sock.sendto(wrong,peer)
            with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as other:other.sendto(reply,peer)
            if mode=='bad':continue
            time.sleep(.1)
        sock.sendto(reply,peer)
try:
    ports=[p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x16c0,0x0483)];assert len(ports)==1,ports
    conn=serial.Serial(ports[0],115200,timeout=.03,write_timeout=3,exclusive=True)
    time.sleep(.5);exchange(b'\r');cmd('lcd key ctrlc');lcd_owned=False;cmd('network up')
    deadline=time.monotonic()+30
    while True:
        status=cmd('network status')
        if 'link=up' in status and 'DHCP=bound' in status:break
        assert time.monotonic()<deadline,status;time.sleep(.5)
    r['network']=status
    match=re.search(r'(?:ip|IP|address)[=: ]+(\d+\.\d+\.\d+\.\d+)',status);assert match,status
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as route:
        route.connect((match[1],9));host=route.getsockname()[0]
    for typ in (socket.SOCK_STREAM,socket.SOCK_DGRAM,socket.SOCK_STREAM):
        sock=socket.socket(socket.AF_INET,typ);sock.bind((host,0));sock.settimeout(.2);servers.append(sock)
    tcp,udp,closed=servers;tcp.listen();tcpport=tcp.getsockname()[1];udpport=udp.getsockname()[1];closedport=closed.getsockname()[1]
    threading.Thread(target=tcp_worker,args=(tcp,),daemon=True).start();threading.Thread(target=ntp_worker,args=(udp,),daemon=True).start()
    r['fixtures']={'host':host,'tcp':tcpport,'udp':udpport,'closed':closedport}
    target=f'{host} {udpport}'
    assert '3 received, 0% loss' in cmd('ping '+host+' 3')
    assert '1 open, 2 probes' in cmd(f'netscan {host} {tcpport},{closedport}')
    assert '1 open, 1 probes' in cmd(f'netscan {host}/32 {tcpport}')
    for bad in ('ping '+host+' 0',f'netscan {host} 0',f'ntp {host} 70000'):
        assert 'usage:' in cmd(bad)
    before=rtc();assert 'ntp: query' in cmd('ntp -q '+target);assert 0<=rtc()-before<=4
    mode='mixed';out=cmd('ntp -q '+target);assert 'ntp: query' in out and re.search(r'[1-9]\d* rejected',out),out
    mode='bad';before=rtc();out=cmd('ntp '+target);assert 'RTC unchanged' in out and 'timed out' in out,out;assert 4<=rtc()-before<=8
    mode='denied';assert 'server denied request; RTC unchanged' in cmd('ntp '+target)
    mode='era';assert 'epoch=2085978500' in cmd('ntp -q '+target)
    mode='valid';assert 'RTC synchronized' in cmd('ntp '+target);assert abs(rtc()-int(time.time()))<=2
    for command in ('ping '+host+' 999',f'netscan {host} 1-128','ntp '+target):
        mode='drop';exchange((command+'\r').encode(),False,.4);out=exchange(b'\x03');assert 'stopped' in out,out
    mode='valid'
    # Warm all paths before exact repeated resource accounting.
    baseline=None;r['memory']=[]
    for i in range(5):
        assert '1 received, 0% loss' in cmd('ping '+host+' 1')
        assert '1 open, 2 probes' in cmd(f'netscan {host} {tcpport},{closedport}')
        assert 'ntp: query' in cmd('ntp -q '+target)
        current=mem();r['memory'].append(current)
        if baseline is None:baseline=current
        else:assert current==baseline,r['memory']
    # USB can still drive the LCD while its long-running ping yields.
    lcd_owned=True;cmd('lcd send '+json.dumps('ping '+host+' 999'));time.sleep(.3)
    assert 'Internal heap:' in cmd('mem')
    out=cmd('ping '+host+' 1')
    # An ICMP slot is held only until each reply/timeout, not between requests.
    assert 'another ping is active' in out or '1 received, 0% loss' in out,out
    cmd('network down');time.sleep(.3);cmd('lcd key ctrlc');lcd_owned=False;cmd('network up')
    deadline=time.monotonic()+30
    while 'DHCP=bound' not in cmd('network status'):
        assert time.monotonic()<deadline;time.sleep(.5)
    assert '1 received, 0% loss' in cmd('ping '+host+' 1')
    r['passed']=True;print('PASS: ICMP replies/cancel, bounded TCP scan, NTP query/sync/rejection/cancel/2036, exact warm memory recovery, concurrent LCD and link restart')
finally:
    stop.set()
    for sock in servers:sock.close()
    if conn:
        if lcd_owned:
            try:cmd('lcd key ctrlc')
            except Exception as error:r['cleanup_error']=str(error)
        conn.close()
    save()
