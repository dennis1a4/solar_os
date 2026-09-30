#!/usr/bin/env python3
"""MQTT Explorer hardware checks against an isolated scripted broker.
Uses unique SD logs, retains fixtures, and controls the LCD: keep keyboard idle.
With --live-auth FILE, additionally connect read-only to --live-host and leave
Explorer running. FILE contains username/password lines and is never logged.
"""
import argparse, json, queue, re, socket, threading, time, uuid
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_shell import ANSI


def packet(header, body=b''):
    result=bytearray([header]);n=len(body)
    while True:
        result.append((n%128)|(128 if n>=128 else 0));n//=128
        if not n:break
    return bytes(result)+body


def publish(topic, payload, qos=0, retained=False):
    topic=topic.encode();body=len(topic).to_bytes(2,'big')+topic
    if qos:body+=b'\x12\x34'
    return packet(0x30|(qos<<1)|int(retained),body+payload)


class Broker:
    def __init__(self):
        self.server=socket.socket();self.server.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
        self.server.bind(('0.0.0.0',0));self.server.listen();self.server.settimeout(.1)
        self.port=self.server.getsockname()[1];self.output=queue.Queue();self.ready=threading.Event()
        self.stop=False;self.connections=0;self.acks=0;self.pings=0;self.errors=[]
        self.thread=threading.Thread(target=self.run,daemon=True);self.thread.start()
    def run(self):
        while not self.stop:
            try:c,_=self.server.accept()
            except socket.timeout:continue
            except OSError:break
            with c:
                c.settimeout(.03);buf=b'';self.connections+=1
                while not self.stop:
                    try:
                        item=self.output.get_nowait()
                        if item is None:break
                        # Force TCP segmentation unrelated to MQTT packet boundaries.
                        for at in range(0,len(item),37):c.sendall(item[at:at+37])
                    except queue.Empty:pass
                    except OSError:break
                    try:
                        data=c.recv(8192)
                        if not data:break
                        buf+=data
                    except socket.timeout:continue
                    except OSError:break
                    while len(buf)>=2:
                        size=0;mult=1;pos=1
                        while pos<len(buf):
                            b=buf[pos];pos+=1;size+=(b&127)*mult;mult*=128
                            if not b&128:break
                        else:break
                        if len(buf)<pos+size:break
                        header=buf[0];body=buf[pos:pos+size];buf=buf[pos+size:]
                        try:
                            if header==0x10:
                                assert body[:7]==b'\0\4MQTT\4' and body[7]&2
                                for b in b'\x20\x02\0\0':c.sendall(bytes([b]));time.sleep(.003)
                            elif header==0x82:
                                assert body[2:]==b'\0\1#\1\0\6$SYS/#\1',body
                                c.sendall(packet(0x90,body[:2]+b'\1\1'));self.ready.set()
                            elif header==0x40:
                                assert body==b'\x12\x34';self.acks+=1
                            elif header==0xc0:
                                assert not body;c.sendall(b'\xd0\0');self.pings+=1
                            elif header==0xe0:break
                            else:raise AssertionError(('unexpected packet',header))
                        except (AssertionError,OSError) as error:self.errors.append(str(error));break
                self.ready.clear()
    def close(self):self.stop=True;self.server.close();self.thread.join(2)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--log',type=Path,required=True)
    p.add_argument('--live-host');p.add_argument('--live-auth',type=Path)
    p.add_argument('--live-only',action='store_true');a=p.parse_args()
    if a.live_auth and not a.live_host:p.error('--live-auth requires --live-host')
    if a.live_only and not a.live_auth:p.error('--live-only requires --live-auth')
    report={'passed':False,'commands':[]};broker=None
    ports=[x.device for x in list_ports.comports() if (x.vid,x.pid)==(0x16c0,0x0483)];assert len(ports)==1,ports
    conn=serial.Serial(ports[0],115200,timeout=.03,write_timeout=3,exclusive=True)
    def save():a.log.write_text(json.dumps(report,indent=2)+'\n')
    def cmd(s,timeout=30,secret=False,python=False):
        conn.write((s+'\r').encode());data=b'';end=time.monotonic()+timeout
        while time.monotonic()<end:
            data+=conn.read(16384);text=ANSI.sub('',data.decode(errors='replace')).replace('\r','')
            if 'Fault IRQ:' in text:raise RuntimeError('Device fault during MQTT test')
            if text.endswith('>>> ') if python else re.search(r'[\w.-]+@[\w.-]+:/[^\n]* $',text):
                if not secret:report['commands'].append({'input':s,'output':text});save()
                return text
        raise RuntimeError('Timeout during private command' if secret else (s,data[-1200:]))
    def local(s):assert 'Queued' in cmd('lcd send '+json.dumps(s));time.sleep(.3)
    def key(s):cmd('lcd key '+s);time.sleep(.2)
    def screen():return cmd('lcd dump')
    def wait_screen(test,timeout=20):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            text=screen()
            if test(text):return text
            time.sleep(.2)
        raise AssertionError(text)
    def count(text):
        m=re.search(r'rx=(\d+)',text);return int(m[1]) if m else -1
    def mem():
        out=cmd('mem');m=re.search(r'Internal heap: (\d+) free / \d+ bytes; PSRAM: (\d+) free',out);assert m,out
        return tuple(map(int,m.groups()))
    def quit():key('exit');wait_screen(lambda s:'MQTT Explorer' not in s)
    try:
        time.sleep(.6);cmd('\x1d');key('exit');cmd('network up')
        assert 'mqttx -' in cmd('apps')
        if not a.live_only:
            with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as route:
                route.connect((a.live_host or '192.168.1.1',1883));host=route.getsockname()[0]
            broker=Broker();target=f'{host}:{broker.port}';name='/sd/mqtt-test-'+uuid.uuid4().hex[:8]+'.jsonl';report['sd_log']=name
            # Warm up allocator/TUI before lifecycle accounting.
            local('mqttx '+target);wait_screen(lambda s:'CONNECTED' in s and 'OFFLINE' not in s);quit();baseline=mem()
            local('mqttx '+target+' --log '+name);wait_screen(lambda s:'CONNECTED' in s and 'SD logging' in s)
            broker.output.put(publish('sensors/temperature',b'{"value":23.5,"unit":"C"}',1,True))
            broker.output.put(publish('sensors/binary',bytes(range(256))))
            broker.output.put(publish('sensors/empty',b''))
            broker.output.put(publish('sensors/large',b'Z'*4096))
            broker.output.put(publish('$SYS/test',b'fixture'))
            wait_screen(lambda s:count(s)>=5 and 'truncated=1' in s)
            assert 'USB_MQTT_OK' in cmd('echo USB_MQTT_OK')
            key('tab');wait_screen(lambda s:'MESSAGE HISTORY' in s)
            key('end');wait_screen(lambda s:'QoS1' in s and 'RETAIN' in s)
            local('j');wait_screen(lambda s:'JSON FORMAT' in s and 'value' in s)
            local('x');wait_screen(lambda s:'PAYLOAD: HEX' in s and '7b' in s)
            local('/binary');wait_screen(lambda s:'binary' in s and 'filtered' in s)
            local('c');key('home')
            key('space');paused=screen();before=count(paused)
            broker.output.put(b''.join(publish(f'burst/{i}',str(i).encode()) for i in range(300)))
            time.sleep(3);assert count(screen())==before,'Pause should freeze the display'
            key('space');wait_screen(lambda s:count(s)>=305 and 'evicted=' in s and 'unindexed=' in s,40)
            connections=broker.connections;broker.output.put(None)
            wait_screen(lambda s:'gaps=1' in s and 'CONNECTED' in s,30)
            assert broker.connections>connections
            # Idle connection requires keepalive and PINGRESP handling.
            deadline=time.monotonic()+25
            while time.monotonic()<deadline and not broker.pings:time.sleep(.2)
            assert broker.pings and broker.acks and not broker.errors,broker.errors
            quit();after=mem();assert after[0]>=baseline[0] and after[1]==baseline[1],(baseline,after)
            local('mqttx '+target+' --log '+name)
            wait_screen(lambda s:'cannot create log' in s)
            # Validate on-device logging without echoing the captured stream.
            cmd('python',python=True)
            cmd('f=open('+repr(name)+');d=f.read();f.close()',python=True,timeout=90)
            out=cmd('print(d)',python=True,secret=True,timeout=90)
            records=[json.loads(line) for line in out.splitlines() if line.startswith('{')]
            assert records and records[-1].get('capture_end') is True
            assert any(x.get('payload_bytes')==4096 and x.get('truncated') for x in records)
            assert any(x.get('payload_hex')==bytes(range(256)).hex() for x in records)
            assert records[-1]['log_lost']==305-(len(records)-1),records[-1]
            report['log_validation']={'records':len(records)-1,'log_lost':records[-1]['log_lost']}
            cmd('\x04')
            after=mem() # Interpreter may initialize persistent libc state once.
            for _ in range(4):
                local('mqttx '+target);wait_screen(lambda s:'CONNECTED' in s and 'OFFLINE' not in s);quit()
            final=mem();assert final[0]>=after[0] and final[1]==after[1],(after,final)
            report['fixture_passed']=True;report['memory']={'before':baseline,'after':final};report['broker']={'connections':broker.connections,'acks':broker.acks,'pings':broker.pings}
            broker.close();broker=None
        if a.live_auth:
            credentials=a.live_auth.read_bytes();assert len(credentials)<160 and credentials.count(b'\n')>=1
            auth='/flash/.mqttx-auth-'+uuid.uuid4().hex[:8]
            cmd('python',python=True)
            # Device echoes REPL input, so neither input nor response is recorded.
            out=cmd('f=open('+repr(auth)+',"x");f.write('+repr(credentials.decode())+');f.close()',python=True,secret=True)
            assert 'Traceback' not in out,'Could not create private auth file'
            cmd('\x04')
            local('mqttx '+a.live_host+':1883 --auth '+auth)
            text=wait_screen(lambda s:'CONNECTED' in s and 'OFFLINE' not in s,30)
            time.sleep(3);text=screen();report['live_received']=count(text);report['auth_file']=auth;report['live_passed']=True
        report['passed']=True;print('PASS:',{k:v for k,v in report.items() if k!='commands'})
    finally:
        if broker:broker.close()
        conn.close();save()

if __name__=='__main__':main()
