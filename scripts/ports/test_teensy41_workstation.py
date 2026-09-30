#!/usr/bin/env python3
"""Workstation acceptance on an idle legacy-wiring Teensy. Retains unique SD fixtures.
Requires exclusive serial access and an idle local keyboard. Starts Ethernet for
an isolated HTTP fixture; does not change RTC, preferences or existing files.
"""
import argparse, hashlib, http.server, json, re, socket, threading, time, uuid
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_hotplug import ANSI, PROMPT


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--log',type=Path,required=True)
    a=ap.parse_args()
    report={'passed':False,'commands':[]}
    ports=[p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x16c0,0x0483)]
    assert len(ports)==1,ports
    conn=serial.Serial(ports[0],115200,timeout=.03,write_timeout=3,exclusive=True)
    server=None
    def save():a.log.write_text(json.dumps(report,indent=2)+'\n')
    def exchange(raw=b'',prompt=True,seconds=30):
        started=time.monotonic();conn.write(raw);data=bytearray();deadline=started+seconds
        while time.monotonic()<deadline:
            data.extend(conn.read(16384))
            out=ANSI.sub('',data.decode(errors='replace')).replace('\r','')
            assert 'Fault IRQ:' not in out,out
            if prompt and PROMPT.search(out):break
        else:
            if prompt:raise TimeoutError((raw,data[-2000:]))
        report['commands'].append({'input':repr(raw),'output':out,'seconds':round(time.monotonic()-started,3)});save();return out
    def cmd(s,seconds=30):return exchange((s+'\r').encode(),seconds=seconds)
    def py(s):
        out=cmd('python -c '+json.dumps(s));assert 'Traceback' not in out,out;return out
    def memory():
        out=cmd('mem');m=re.search(r'Internal heap: (\d+) free / \d+ bytes; PSRAM: (\d+) free',out)
        assert m,out
        return tuple(map(int,m.groups()))
    try:
        time.sleep(.8);cmd('\x1d');cmd('lcd key exit')
        available=cmd('commands')
        for name in ('man','watch','session','sessions','top','df','version','board','status','port','pwd','date','time','zip','unzip'):
            assert re.search(r'\b'+name+r'\b',available),name
        for s,expected in [('version','Teensy'),('board','Cortex-M7'),('pwd','/'),('status','Last foreground exit'),
                           ('top','Stack-free'),('port','usb'),('session list','usb-shell'),('sessions','lcd-shell'),
                           ('date',r'20\d\d-\d\d-\d\d'),('time',r'\d\d:\d\d:\d\d')]:
            out=cmd(s);assert re.search(expected,out),(s,out)
        out=cmd('df',seconds=90);assert '/flash' in out and '/sd' in out,out
        assert 'watch' in cmd('man --list')
        assert 'watch' in cmd('man -k repeat')
        for key in (b'q',b'\x1b',b'\x03',b'\x1d'):
            out=exchange(b'watch -n 1 uptime\r',False,2.3)
            readings=re.findall(r'Uptime=(\d+)',out)
            assert len(set(readings))>=2,out
            assert 'watch stopped' in exchange(key),key
        assert 'nested watch' in exchange(b'watch watch uptime\r',False,.5)
        exchange(b'q')
        assert 'interval' in cmd('watch -n 0 uptime')
        # Warm up the pager, then verify repeated manual lifecycle and task snapshots.
        out=exchange(b'man watch\r',False,.8);assert 'watch' in out.lower(),out
        exchange(b'q');base=memory()
        for _ in range(5):
            exchange(b'man watch\r',False,.4);exchange(b'q');cmd('top');cmd('session list')
        after=memory();assert after==base,(base,after)
        report['manual_memory_before']=base;report['manual_memory_after']=after
        out=exchange(b'help\r',False,.7);assert 'Commands' in out or 'Applications' in out,out
        exchange(b'q')
        # Shell ticks must also run on LCD, without blocking USB.
        cmd('lcd send "watch -n 1 uptime"');time.sleep(.5)
        one=cmd('lcd dump');time.sleep(1.2);two=cmd('lcd dump')
        assert re.findall(r'Uptime=\d+',one)!=re.findall(r'Uptime=\d+',two),(one,two)
        assert 'USB_RESPONSIVE' in cmd('echo USB_RESPONSIVE')
        cmd('lcd key exit')
        root='/sd/workstation-'+uuid.uuid4().hex[:8];report['fixture']=root
        cmd('mkdir '+root)
        py("f=open('%s/source.txt','wb');f.write(b'workstation archive test\\n');f.close()"%root)
        out=cmd('zip '+root+'/test.zip '+root+'/source.txt',seconds=60);assert 'failed' not in out,out
        assert 'source.txt' in cmd('unzip -l '+root+'/test.zip')
        cmd('mkdir '+root+'/out');out=cmd('unzip '+root+'/test.zip '+root+'/out',seconds=60);assert 'failed' not in out,out
        py("f=open('%s/out/source.txt','rb');assert f.read()==b'workstation archive test\\n';f.close()"%root)
        # Local HTTP server, no credentials or public network dependency.
        payload=b'WORKSTATION_HTTP_OK\n'+bytes(range(256))*4
        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                data=b'WORKSTATION_HTTP_OK\n' if self.path=='/text' else payload
                self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers()
                if self.path=='/slow':time.sleep(4)
                try:self.wfile.write(data)
                except (BrokenPipeError,ConnectionResetError):pass
            def log_message(self,*args):pass
        server=http.server.ThreadingHTTPServer(('0.0.0.0',0),Handler)
        threading.Thread(target=server.serve_forever,daemon=True).start()
        cmd('network up');time.sleep(2)
        net=cmd('network status');matches=re.findall(r'\b(?:\d{1,3}\.){3}\d{1,3}\b',net)
        host=next(x for x in matches if x!='0.0.0.0')
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as route:
            route.connect((host,80));local=route.getsockname()[0]
        url='http://%s:%d'%(local,server.server_address[1])
        out=exchange(('curl '+url+'/text\r').encode(),seconds=45);assert 'WORKSTATION_HTTP_OK' in out,out
        assert '[2]' not in out,out
        out=cmd('curl -o '+root+'/http.bin '+url+'/binary',seconds=45);assert 'failed' not in out,out
        py("import hashlib;f=open('%s/http.bin','rb');assert hashlib.sha256(f.read()).hexdigest()=='%s';f.close()"%(root,hashlib.sha256(payload).hexdigest()))
        base=memory()
        for _ in range(5):
            out=cmd('curl '+url+'/text',seconds=45);assert 'WORKSTATION_HTTP_OK' in out,out
        after=memory();assert after==base,(base,after)
        report['curl_memory_before']=base;report['curl_memory_after']=after
        exchange(('curl '+url+'/slow\r').encode(),False,.5)
        exchange(b'\x03');assert 'Last foreground exit: 130' in cmd('status')
        # Graceful TCP close can retain transport buffers until the delayed
        # peer finishes. Require bounded, exact recovery without a network reset.
        deadline=time.monotonic()+10
        after=memory()
        while after!=base and time.monotonic()<deadline:
            time.sleep(.2);after=memory()
        assert after==base,(base,after)
        report['cancel_memory_after']=after
        cmd('lcd send '+json.dumps('curl '+url+'/text'));time.sleep(1)
        out=cmd('echo USB_CURL_ISOLATION');assert 'solar_os_curl:' not in out,out
        assert 'WORKSTATION_HTTP_OK' in cmd('lcd dump')
        report['passed']=True
        print('PASS: manual, watch exits/ticks, diagnostics, sessions, archives, HTTP and memory cleanup')
    finally:
        if server:server.shutdown();server.server_close()
        conn.close();save()

if __name__=='__main__':main()
