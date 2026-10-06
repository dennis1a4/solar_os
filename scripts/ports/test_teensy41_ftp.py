#!/usr/bin/env python3
"""Live FTP server acceptance. Uses a unique SD directory and removes it on success.
Requires pyserial, idle consoles, mounted SD, and Ethernet on the host's LAN.
"""
import argparse
import ftplib
import io
import json
import re
import socket
import time
import uuid
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_hotplug import Console as BaseConsole, require, ANSI


class Console(BaseConsole):
    def command(self, command, python=False):
        self.port.write((command+"\r").encode());data=bytearray();deadline=time.monotonic()+30;last=time.monotonic()
        while time.monotonic()<deadline:
            chunk=self.port.read(16384)
            if chunk:data.extend(chunk);last=time.monotonic()
            text=ANSI.sub("",data.decode(errors="replace")).replace("\r", "")
            if any(marker in text for marker in ("Fault IRQ:", "STACK OVERFLOW", "ASSERT in")):
                self.report["commands"].append(dict(command=command,output=text,fault=True))
                require(False,text)
            ready=text.endswith(">>> ") if python else re.search(r"[\w.-]+@[\w.-]+:/[^\n]* ",text)
            if ready and time.monotonic()-last>.1:
                self.report["commands"].append(dict(command=command,output=text));self.python=python;return text
        self.report["commands"].append(dict(command=command,output=text,timeout=True));raise TimeoutError(command)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log',type=Path,required=True)
    parser.add_argument('--port',type=int,default=2121)
    args=parser.parse_args()
    root='/sd/_ftp_'+uuid.uuid4().hex[:8]
    report=dict(passed=False,commands=[],checks=[],memory={},fixture=root)
    ftp=None
    def passed(name):
        report['checks'].append(name);print('PASS:',name,flush=True)
        args.log.write_text(json.dumps(report,indent=2)+'\n')
    try:
        ports=[p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x16c0,0x0483)]
        require(len(ports)==1,ports)
        with serial.Serial(ports[0],115200,timeout=.03,write_timeout=3,exclusive=True) as usb:
            c=Console(usb,report);c.command('')
            require('ftpd stopped' in c.command('job status ftpd'),'FTP already active or missing')
            require('mounted' in c.command('sd status'),'SD unavailable')
            c.command('network up')
            out=c.poll('network status',lambda s:'link=up' in s and 'address=0.0.0.0' not in s)
            host=re.search(r'address=(\d+\.\d+\.\d+\.\d+)',out).group(1);report['ip']=host
            require('mkdir:' not in c.command('mkdir '+root),'fixture creation')
            def memory(label):
                time.sleep(.25);out=c.command('mem')
                match=re.search(r'Internal heap: (\d+) free / (\d+) bytes; PSRAM: (\d+) free / (\d+)',out)
                require(match is not None,out);report['memory'][label]=list(map(int,match.groups()))
                return report['memory'][label]
            def start(auth=True):
                command=f'job start ftpd {root} {args.port}'+(' --user test --password ftp-test' if auth else '')
                out=c.command(command);require('ftpd: OK' in out,out)
                require('ftpd running' in c.command('job status ftpd'),'start failed')
            def stop():
                out=c.command('job stop ftpd');require('ftpd: OK' in out,out)
                c.poll('job status ftpd',lambda s:'stopped' in s)
            def connect(auth=True,epsv=False):
                client=ftplib.FTP();client.connect(host,args.port,timeout=20)
                client.login('test' if auth else 'anonymous','ftp-test' if auth else 'test@')
                if epsv:
                    def passive():return host,ftplib.parse229(client.sendcmd('EPSV'),(host,args.port))[1]
                    client.makepasv=passive
                return client
            memory('cold_idle');start();memory('server_running')
            bad=ftplib.FTP();bad.connect(host,args.port,timeout=10)
            try:bad.login('test','wrong');raise AssertionError('bad password accepted')
            except ftplib.error_perm as e:require(str(e).startswith('530'),str(e))
            finally:bad.close()
            passed('password rejection')
            ftp=connect()
            payload=bytes(range(256))*257+b'\0\r\nend'
            start_time=time.monotonic();ftp.storbinary('STOR binary.bin',io.BytesIO(payload))
            report['upload_seconds']=time.monotonic()-start_time
            result=io.BytesIO();start_time=time.monotonic();ftp.retrbinary('RETR binary.bin',result.write)
            report['download_seconds']=time.monotonic()-start_time
            require(result.getvalue()==payload,'binary mismatch');require(ftp.size('binary.bin')==len(payload),'size mismatch')
            require('binary.bin' in dict(ftp.mlsd()),'MLSD');listing=[];ftp.retrlines('LIST',listing.append)
            require(any('binary.bin' in line for line in listing),'LIST');passed('PASV binary round trip, SIZE, MLSD and LIST')
            ftp.mkd('folder');ftp.rename('binary.bin','folder/renamed.bin');ftp.cwd('folder');require(ftp.pwd()=='/folder','CWD')
            result=io.BytesIO();ftp.retrbinary('RETR renamed.bin',result.write);require(result.getvalue()==payload,'rename corrupt')
            ftp.cwd('..');ftp.delete('folder/renamed.bin');ftp.rmd('folder');ftp.cwd('../../..');require(ftp.pwd()=='/','root confinement')
            try:ftp.retrbinary('RETR /../../flash/settings.json',lambda _:None);raise AssertionError('escaped root')
            except ftplib.error_perm:pass
            passed('mkdir/CWD/rename/delete/rmdir and root confinement')
            for name,body in [('empty',b''),('kept.bin',b'original')]:ftp.storbinary('STOR '+name,io.BytesIO(body))
            result=io.BytesIO();ftp.retrbinary('RETR empty',result.write);require(result.getvalue()==b'','empty file')
            ftp.quit();ftp=None;ftp=connect(epsv=True);result=io.BytesIO();ftp.retrbinary('RETR kept.bin',result.write)
            require(result.getvalue()==b'original','EPSV');passed('empty files and EPSV')
            data=ftp.transfercmd('STOR kept.bin');data.sendall(b'partial')
            time.sleep(.1);then=time.monotonic();stop();report['stalled_stop_seconds']=time.monotonic()-then
            require(report['stalled_stop_seconds']<3,'slow stop');data.close();ftp.close();ftp=None
            start();ftp=connect();result=io.BytesIO();ftp.retrbinary('RETR kept.bin',result.write)
            require(result.getvalue()==b'original','stop replaced original');require(not any('.ftp-' in n for n,_ in ftp.mlsd()),'staging leak')
            passed('stalled upload stop preserves destination and removes staging file')
            ftp.quit();ftp=None;stop();memory('warm_idle')
            for cycle in range(10):
                start(auth=False);ftp=connect(auth=False)
                ftp.storbinary('STOR repeat.bin',io.BytesIO(payload[:4096]));result=io.BytesIO();ftp.retrbinary('RETR repeat.bin',result.write)
                require(result.getvalue()==payload[:4096],cycle);ftp.delete('repeat.bin');ftp.quit();ftp=None;stop()
            before=report['memory']['warm_idle'];after=memory('after_10_cycles')
            require(before==after,('heap recovery',before,after));passed('10 anonymous start/transfer/stop cycles with exact heap recovery')
            start();ftp=connect();c.command('network down');ftp.close();ftp=None
            c.poll('job status ftpd',lambda s:'failed' in s or 'stopped' in s)
            c.command('network up');c.poll('network status',lambda s:'link=up' in s and 'address=0.0.0.0' not in s,timeout=25)
            start();ftp=connect();require('kept.bin' in dict(ftp.mlsd()),'reconnect');passed('network down/up and server restart')
            for name,_ in list(ftp.mlsd()):ftp.delete(name)
            ftp.quit();ftp=None;stop();require('rmdir:' not in c.command('rm -r '+root),'fixture cleanup')
            report['final_sd']=c.command('sd status');memory('final_idle');report['passed']=True
    finally:
        if ftp:ftp.close()
        args.log.write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':main()
