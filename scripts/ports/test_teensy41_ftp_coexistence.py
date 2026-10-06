#!/usr/bin/env python3
"""Live FTP/Telnet/Python coexistence, port conflicts and dry-run shutdown."""
import argparse
import ftplib
import io
import json
import re
import secrets
import threading
import time
import uuid
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_ftp import Console
from test_teensy41_hotplug import PROMPT,require
from test_teensy41_telnet import Telnet


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--log',type=Path,required=True);args=p.parse_args()
    root='/sd/_ftpm_'+uuid.uuid4().hex[:8];report=dict(passed=False,commands=[],checks=[],fixture=root)
    ftp=None;remote=None
    def passed(name):report['checks'].append(name);print('PASS:',name,flush=True)
    try:
        ports=[p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x16c0,0x0483)];require(len(ports)==1,ports)
        with serial.Serial(ports[0],115200,timeout=.03,write_timeout=3,exclusive=True) as usb:
            c=Console(usb,report);c.command('');require('ftpd stopped' in c.command('job status ftpd'),'existing FTP job');require('stopped' in c.command('telnetd status'),'existing Telnet service')
            out=c.command('network status');host=re.search(r'address=(\d+\.\d+\.\d+\.\d+)',out).group(1)
            c.command('mkdir '+root)
            def start(port=2121):return c.command(f'job start ftpd {root} {port} --user test --password ftp-test')
            def connect():
                f=ftplib.FTP();f.connect(host,2121,timeout=20);f.login('test','ftp-test');return f
            require('ftpd: OK' in start(),'server start');ftp=connect();secret=secrets.token_hex(12)
            ftp.storbinary('STOR telnet.pass',io.BytesIO((secret+'\n').encode()));ftp.quit();ftp=None
            out=c.command(f'telnetd start {root}/telnet.pass 2121');require('listening' not in out,'Telnet reused FTP port')
            require('listening' in c.command(f'telnetd start {root}/telnet.pass 2323'),'Telnet start')
            c.command('job stop ftpd');require('ftpd: OK' not in start(2323),'FTP reused Telnet port');require('ftpd: OK' in start(),'restart after conflict');passed('FTP/Telnet port conflicts rejected in both directions')
            remote=Telnet(host,2323);remote.read(lambda s:'Password: ' in s);remote.send(secret.encode()+b'\r\n');remote.read(lambda s:bool(PROMPT.search(s)))
            ftp=connect();payload=bytes(range(256))*513;errors=[]
            def transfer():
                try:
                    ftp.storbinary('STOR concurrent.bin',io.BytesIO(payload));result=io.BytesIO();ftp.retrbinary('RETR concurrent.bin',result.write);require(result.getvalue()==payload,'concurrent mismatch')
                except BaseException as exc:errors.append(repr(exc))
            thread=threading.Thread(target=transfer);thread.start();time.sleep(.25)
            then=time.monotonic();remote.send(b'echo FTP_TELNET_ALIVE\r\n');text=remote.read(lambda s:bool(PROMPT.search(s)))
            report['telnet_echo_seconds']=time.monotonic()-then;require('FTP_TELNET_ALIVE' in text,text)
            remote.send(b'python -c "print(12345+1)"\r\n');text=remote.read(lambda s:bool(PROMPT.search(s)),timeout=15);require('12346' in text,text)
            thread.join(30);require(not thread.is_alive() and not errors,errors);passed('binary FTP round trip while Telnet shell and Python remain usable')
            report['tasks_after_transfer']=c.command('top');report['memory_running']=c.command('mem')
            ftp.delete('concurrent.bin');ftp.quit();ftp=None;remote.send(b'exit\r\n');remote.close();remote=None;c.command('telnetd stop')
            require('Shutdown requested' in c.command('poweroff --check'),'dry-run shutdown')
            report['shutdown']=c.poll('poweroff status',lambda s:'check complete' in s,timeout=15)
            require('ftpd stopped' in c.command('job status ftpd'),'shutdown left FTP running');passed('dry-run poweroff stops FTP before storage sync')
            require('ftpd: OK' in start(),'restart after shutdown check');ftp=connect();ftp.delete('telnet.pass');ftp.quit();ftp=None;c.command('job stop ftpd');c.command('rm -r '+root)
            report['final_sd']=c.command('sd status');report['final_memory']=c.command('mem');report['passed']=True
    finally:
        if ftp:ftp.close()
        if remote:remote.close()
        args.log.write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':main()
