#!/usr/bin/env python3
"""DNP3 acceptance using the LCD app and two host-owned TCP connections.
Requires exclusive USB, idle LCD shell, Ethernet, and mounted SD. No external
DNP3 devices are contacted. Serial electrical/transceiver tests remain manual.
"""
import argparse,json,re,socket,time,uuid
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_ftp import Console

def crc(data):
    value=0
    for b in data:
        value^=b
        for _ in range(8):value=(value>>1)^(0xa6bc if value&1 else 0)
    return (~value)&65535

def frame(body,src=1,dst=1024,control=0xc4):
    h=bytes([5,100,len(body)+5,control])+dst.to_bytes(2,'little')+src.to_bytes(2,'little')
    p=h+crc(h).to_bytes(2,'little')
    for offset in range(0,len(body),16):
        block=body[offset:offset+16];p+=block+crc(block).to_bytes(2,'little')
    return p

def exact(s,n):
    out=b''
    while len(out)<n:
        part=s.recv(n-len(out));assert part,('EOF',n,len(out));out+=part
    return out

def read_frame(s,valid=True):
    h=exact(s,10);assert h[:2]==b'\x05\x64';n=h[2]-5
    p=h+exact(s,n+2*((n+15)//16))
    if valid:unpack(p)
    return p

def unpack(p):
    assert crc(p[:8])==int.from_bytes(p[8:10],'little'),p.hex()
    out=b'';left=p[2]-5;at=10
    while left:
        n=min(left,16);block=p[at:at+n];assert crc(block)==int.from_bytes(p[at+n:at+n+2],'little');out+=block;left-=n;at+=n+2
    assert at==len(p);return out

def control(fn=5,sequence=0,index=4,value=True):
    return bytes([0xc0,0xc0|sequence,fn,12,1,0x28,1,0])+index.to_bytes(2,'little')+bytes([3 if value else 4,1])+bytes([100,0,0,0])*2+b'\0'

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--log',type=Path,required=True);parser.add_argument('--port-a',type=int,default=22001);parser.add_argument('--port-b',type=int,default=22002);args=parser.parse_args()
    report={'passed':False,'commands':[],'checks':[]};connections=[];opened=False
    ports=[p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x16c0,0x0483)];assert len(ports)==1,ports
    root='/sd/_dnp3_'+uuid.uuid4().hex[:8];report['fixture']=root
    def passed(name):report['checks'].append(name);args.log.write_text(json.dumps(report,indent=2)+'\n');print('PASS:',name,flush=True)
    try:
        with serial.Serial(ports[0],115200,timeout=.03,write_timeout=3,exclusive=True) as usb:
            c=Console(usb,report);c.command('');out=c.command('network status');host=re.search(r'address=(\d+\.\d+\.\d+\.\d+)',out).group(1);assert host!='0.0.0.0',out
            def local(s):assert 'Queued' in c.command('lcd send '+json.dumps(s));time.sleep(.3)
            def key(s):assert 'Queued' in c.command('lcd key '+s);time.sleep(.3)
            def action(s):local(':'+s)
            def screen():return c.command('lcd dump')
            def connect(port):
                end=time.monotonic()+5
                while True:
                    try:s=socket.create_connection((host,port),timeout=2);s.settimeout(3);connections.append(s);return s
                    except OSError:
                        if time.monotonic()>=end:raise
                        time.sleep(.1)
            def close_all():
                while connections:connections.pop().close()
            # The boot script may consume keyboard input until it completes.
            ready='DNP3_READY_'+uuid.uuid4().hex[:8]
            local('echo '+ready);c.poll('lcd dump',lambda out:ready in out,timeout=20)
            c.command('mkdir '+root)
            local('dnp3 --demo');opened=True;c.poll('lcd dump',lambda out:'DNP3 demo RUN' in out,timeout=15)
            action('interval 0');action('point ai 3 -1234');action('point bi 3 1');action('control binary 4 1');time.sleep(.2)
            key('tab');out=screen();assert 'controls:1' in out and 'timeout:0 errors:0' in out,out
            key('tab');out=screen();assert '03 BI:1 AI:-1234' in out and '04 BI:0 AI:0 BO:1' in out,out
            key('ctrlz');out=c.command('sessions');m=re.search(r'^(\d+)\s+lcd-shell\s+suspended\s+dnp3',out,re.M);assert m,out
            action_id=m[1];assert 'Queued' in c.command('fg '+action_id);time.sleep(.3);assert 'DNP3 demo RUN' in screen()
            passed('on-device demo: editable points, select/operate control, retained session')
            action('stop');action('save '+root+'/profile.cfg');action('point ai 3 999');action('load '+root+'/profile.cfg');assert '03 BI:1 AI:-1234' in screen()
            action('mode bridge');action(f'a tcp listen {args.port_a}');action(f'b tcp listen {args.port_b}');action('log '+root+'/traffic.tsv');action('start');a=connect(args.port_a);b=connect(args.port_b)
            request=frame(bytes([0xc0,0xc0,1,60,1,6]));
            for byte in request:a.sendall(bytes([byte]))
            assert exact(b,len(request))==request
            raw=b'not DNP3\x00\xff\x05\x64badCRC';b.sendall(raw);assert exact(a,len(raw))==raw
            passed('bidirectional TCP passthrough: byte-split frame and corrupt/raw traffic exact')
            action('rule add control a2b 4 12');packet=frame(control());a.sendall(packet);rewritten=read_frame(b);body=unpack(rewritten);assert int.from_bytes(body[8:10],'little')==12
            action('rule add binary b2a 7 1');action('rule add analog b2a 3 -1234');response=frame(bytes([0xc0,0xc0,129,0,0,1,2,0,7,7,1,30,1,0,3,3,1,0,0,0,0]),1024,1,0x44);b.sendall(response);body=unpack(read_frame(a));assert body[10]==129 and int.from_bytes(body[17:21],'little',signed=True)==-1234
            action('fault a2b crc 1');a.sendall(packet);bad=read_frame(b,False);assert crc(bad[:8])!=int.from_bytes(bad[8:10],'little');action('fault clear');action('rule clear')
            passed('live control index, binary/analog status edits, CRC rebuilding and injected CRC failure')
            a.sendall(request);a.shutdown(socket.SHUT_WR);assert exact(b,len(request))==request;assert b.recv(1)==b'';b.sendall(response);b.shutdown(socket.SHUT_WR);assert exact(a,len(response))==response;assert a.recv(1)==b'';close_all();time.sleep(.2);assert 'bridge STOP' in screen();action('log stop');out=c.command('cat '+root+'/traffic.tsv');assert '\tEDIT\t' in out and '\tFAULT\t' in out and 'log_lost=' in c.command('tail -n 1 '+root+'/traffic.tsv'),out
            passed('TCP half-close drains both directions; SD log contains edit/fault audit')
            action('mode outstation');action('start');a=connect(args.port_a);a.sendall(request);reply=unpack(read_frame(a));assert reply[2]==129 and reply[3:5]==b'\0\0'
            # Independent master sends direct and select/operate commands.
            a.sendall(frame(control(5,1,5)));reply=unpack(read_frame(a));assert reply[-1]==0
            a.sendall(frame(control(3,2,6)));assert unpack(read_frame(a))[-1]==0
            a.sendall(frame(control(4,3,6)));assert unpack(read_frame(a))[-1]==0
            close_all();action('stop');passed('TCP outstation serves class-0 scan and independent master direct/SBO controls')
            action('mode master');listener=socket.socket();listener.bind(('',0));listener.listen(1);listener.settimeout(5)
            route=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);route.connect((host,20000));local_ip=route.getsockname()[0];route.close()
            action(f'b tcp connect {local_ip} {listener.getsockname()[1]}');action('start');b,_=listener.accept();listener.close();b.settimeout(3);connections.append(b);action('poll');poll=unpack(read_frame(b));assert poll[2:]==bytes([1,60,1,6]);seq=poll[1]&15;b.sendall(frame(bytes([0xc0,0xc0|seq,129,0,0,1,2,0,0,0,129]),1024,1,0x44));time.sleep(.1)
            action('control binary 2 1');sel=unpack(read_frame(b));assert sel[2]==3;b.sendall(frame(bytes([0xc0,0xc0|(sel[1]&15),129,0,0])+sel[3:],1024,1,0x44));op=unpack(read_frame(b));assert op[2]==4;b.sendall(frame(bytes([0xc1,0xc0|(op[1]&15),129,0,0])+op[3:],1024,1,0x44));time.sleep(.2);close_all();action('stop');passed('TCP master class-0 scan and verified SELECT-before-OPERATE with independent peer')
            key('exit');opened=False
            c.command('rm '+root+'/profile.cfg');c.command('rm '+root+'/traffic.tsv');c.command('rm -r '+root)
            report['memory']=c.command('mem');report['passed']=True
            # Failed earlier runs leave their unique fixtures for inspection.
            # This run removes only the files it created.
            local('dnp3 --demo');opened=False # Leave the useful demo visible.
    finally:
        while connections:connections.pop().close()
        if opened:
            try:
                with serial.Serial(ports[0],115200,timeout=.03,write_timeout=3,exclusive=True) as cleanup:
                    Console(cleanup,report).command('lcd key exit')
            except Exception as error:report['cleanup_error']=str(error)
        args.log.write_text(json.dumps(report,indent=2)+'\n')
    print('PASS: DNP3 device acceptance; physical UART/transceiver test remains manual',flush=True)
if __name__=='__main__':main()
