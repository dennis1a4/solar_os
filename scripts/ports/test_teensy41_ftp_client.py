#!/usr/bin/env python3
"""Exercise the real FTP TUI against pyftpdlib, while Teensy ftpd verifies files.
Uses unique SD and host temporary fixtures, and requires idle USB/LCD consoles.
"""
import argparse
import ftplib
import io
import json
import logging
import re
import socket
import tempfile
import threading
import time
import uuid
from pathlib import Path
import pyte
import serial
from serial.tools import list_ports
from pyftpdlib.authorizers import DummyAuthorizer
from pyftpdlib.handlers import FTPHandler
from pyftpdlib.servers import FTPServer
from test_teensy41_ftp import Console
from test_teensy41_hotplug import ANSI, PROMPT, require


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--log',type=Path,required=True)
    parser.add_argument('--music',help='Existing SD music folder to play on the LCD console during transfers')
    args=parser.parse_args()
    report=dict(passed=False,commands=[],checks=[],screens=[],memory={});root='/sd/_ftpc_'+uuid.uuid4().hex[:8];report['fixture']=root
    server=None;board=None;stall=None;stall_client=None
    def passed(name):
        report['checks'].append(name);print('PASS:',name,flush=True);args.log.write_text(json.dumps(report,indent=2)+'\n')
    try:
        with tempfile.TemporaryDirectory(prefix='solaros-ftp-client-') as temporary:
            host_root=Path(temporary);download=bytes(range(256))*259+b'FTP download\0';upload=b'FTP upload\0'+bytes(range(255,-1,-1))*131
            (host_root/'download.bin').write_bytes(download)
            ports=[p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x16c0,0x0483)];require(len(ports)==1,ports)
            with serial.Serial(ports[0],115200,timeout=.02,write_timeout=3,exclusive=True) as usb:
                c=Console(usb,report);c.command('');require('ftpd stopped' in c.command('job status ftpd'),'existing FTP server')
                player_id=None
                def check_audio():
                    if player_id:
                        out=c.command('audio status')
                        require('running=1 paused=0' in out and 'underruns=0' in out,out)
                if args.music:
                    sessions=c.command('sessions')
                    require(not re.search(r'lcd-shell\s+(?:active|suspended)',sessions),sessions)
                    c.command('lcd send '+json.dumps('player --repeat one '+json.dumps(args.music)))
                    time.sleep(2);c.command('lcd key ctrlz');time.sleep(.3)
                    sessions=c.command('sessions')
                    match=re.search(r'^(\d+)\s+lcd-shell\s+suspended\s+player\b',sessions,re.M)
                    require(match,sessions);player_id=match[1];check_audio()
                out=c.command('network status');board_ip=re.search(r'address=(\d+\.\d+\.\d+\.\d+)',out).group(1)
                with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as route:route.connect((board_ip,9));host_ip=route.getsockname()[0]
                authorizer=DummyAuthorizer();authorizer.add_user('test','ftp-test',str(host_root),perm='elradfmwMT')
                class Handler(FTPHandler):pass
                Handler.authorizer=authorizer;logging.getLogger('pyftpdlib').setLevel(logging.ERROR)
                server=FTPServer((host_ip,0),Handler);host_port=server.socket.getsockname()[1]
                threading.Thread(target=server.serve_forever,kwargs={'timeout':.02,'blocking':True,'handle_exit':False},daemon=True).start()
                c.command('mkdir '+root);c.command('cd '+root)
                require('ftpd: OK' in c.command(f'job start ftpd {root} 2121 --user test --password ftp-test'),'start verifier')
                board=ftplib.FTP();board.connect(board_ip,2121,timeout=20);board.login('test','ftp-test')
                screen=pyte.Screen(80,24);stream=pyte.Stream(screen)
                def ui(raw=b'',expected=None,prompt=False,timeout=30):
                    if raw:usb.write(raw)
                    data=bytearray();deadline=time.monotonic()+timeout;last=time.monotonic()
                    while time.monotonic()<deadline:
                        chunk=usb.read(16384)
                        if chunk:data.extend(chunk);stream.feed(chunk.decode(errors='replace'));last=time.monotonic()
                        text=ANSI.sub('',data.decode(errors='replace')).replace('\r','');display='\n'.join(screen.display)
                        require('Fault IRQ:' not in text and 'STACK OVERFLOW' not in text and 'ASSERT in' not in text,text)
                        ready=bool(PROMPT.search(text)) if prompt else time.monotonic()-last>.2
                        matches = screen.display[22].strip()==expected if expected in ('connected','done') else expected is None or expected in display
                        if ready and matches:
                            report['screens'].append(dict(input=repr(raw),screen=display));return text
                    report['screens'].append(dict(input=repr(raw),screen=display,raw=text,timeout=True));raise TimeoutError(display)
                def open_client():ui(f'ftp {host_ip} {host_port} --user test --password ftp-test\r'.encode(),expected='connected')
                def select(name,pane):
                    ui(b'\x1b[H');col=1 if pane==0 else 41
                    for _ in range(30):
                        for row in range(2,21):
                            if screen.buffer[row][col].reverse:
                                entry=''.join(screen.buffer[row][x].data for x in range(col,col+29)).strip().rstrip('/')
                                if entry==name:return
                        ui(b'\x1b[B')
                    raise AssertionError(('entry missing',name,'\n'.join(screen.display)))
                def memory(label):
                    out=c.command('mem');m=re.search(r'Internal heap: (\d+) free / (\d+) bytes; PSRAM: (\d+) free / (\d+)',out);require(m is not None,out)
                    report['memory'][label]=list(map(int,m.groups()));return report['memory'][label]
                memory('before_client');open_client();ui(b'\t');select('download.bin',1);ui(b'c',expected='done',timeout=45)
                result=io.BytesIO();board.retrbinary('RETR download.bin',result.write);require(result.getvalue()==download,'download differs');passed('TUI download from independent FTP server while Teensy ftpd runs')
                ui(b'\x1d',prompt=True);check_audio();board.storbinary('STOR upload.bin',io.BytesIO(upload))
                open_client();select('upload.bin',0);ui(b'c',expected='done',timeout=45);require((host_root/'upload.bin').read_bytes()==upload,'upload differs')
                passed('TUI upload to independent server, byte-exact')
                ui(b'\t');ui(b'k',expected='mkdir:');ui(b'newdir\r',expected='done');require((host_root/'newdir').is_dir(),'remote mkdir')
                select('newdir',1);ui(b'd',expected='y/N');ui(b'y',expected='done');require(not (host_root/'newdir').exists(),'remote delete')
                passed('TUI remote mkdir and delete');ui(b'\x1d',prompt=True);check_audio()
                memory('warm_idle')
                for _ in range(10):open_client();ui(b'\x1d',prompt=True)
                # TCP close is asynchronous; audio changes the scheduling of
                # the final FIN/close. Still require exact bounded recovery.
                deadline=time.monotonic()+5
                while memory('after_10_clients')!=report['memory']['warm_idle']:
                    require(time.monotonic()<deadline,('client heap',report['memory']))
                passed('10 client connect/exit cycles with exact heap recovery')
                check_audio()
                stall=socket.socket();stall.bind((host_ip,0));stall.listen(1);stall.settimeout(10)
                usb.write(f'ftp {host_ip} {stall.getsockname()[1]}\r'.encode());stall_client,_=stall.accept();ui(expected='connecting')
                then=time.monotonic();ui(b'\x1d',prompt=True,timeout=4);report['cancel_seconds']=time.monotonic()-then
                require(report['cancel_seconds']<2,'cancel delayed');stall_client.close();stall_client=None;stall.close();stall=None
                passed('cancel stalled server greeting promptly')
                check_audio()
                after=memory('after_cancel');before=report['memory']['warm_idle']
                require(after[0]>=before[0] and after[2]==before[2],('cancel heap',report['memory']))
                for name,_ in list(board.mlsd()):board.delete(name)
                board.quit();board=None;c.command('job stop ftpd');c.command('cd /')
                if player_id:
                    for _ in range(5):
                        require('ftpd: OK' in c.command(f'job start ftpd {root} 2121 --user test --password ftp-test'),'restart verifier')
                        with ftplib.FTP() as probe:
                            probe.connect(board_ip,2121,timeout=20);probe.login('test','ftp-test');list(probe.mlsd())
                        require('ftpd: OK' in c.command('job stop ftpd'),'stop verifier')
                        check_audio()
                    passed('5 FTP server task cleanup cycles during MP3 playback')
                    check_audio();c.command('close '+player_id);time.sleep(.4)
                    passed('MP3 playback survives FTP downloads, uploads, reconnects and cancellation without underruns')
                c.command('rm -r '+root)
                memory('final_idle')
                report['final_sd']=c.command('sd status');report['passed']=True
    finally:
        if board:board.close()
        if stall_client:stall_client.close()
        if stall:stall.close()
        if server:server.close_all()
        args.log.write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':main()
