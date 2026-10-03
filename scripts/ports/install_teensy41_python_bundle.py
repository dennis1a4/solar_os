#!/usr/bin/env python3
"""Install versioned Python files via SolarOS serial, with per-file SHA256 checks.
Existing different files are preserved unless --replace is explicitly supplied.
Can also copy a locally cached upstream ZIP to SD for offline browsing/extraction.
"""
import argparse,base64,hashlib,json,re,time
from pathlib import Path
import serial
from serial.tools import list_ports

class Board:
    def __init__(self):
        ports=[p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x16c0,0x0483)]
        if len(ports)!=1: raise RuntimeError('Connect exactly one Teensy console')
        self.c=serial.Serial(ports[0],115200,timeout=.005,write_timeout=5,exclusive=True)
        time.sleep(.5)
        self.command('')
    def read(self, marker=None, timeout=30):
        data=b'';until=time.monotonic()+timeout
        while time.monotonic()<until:
            data+=self.c.read(8192)
            text=re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]','',data.decode(errors='replace')).replace('\r','')
            if 'Fault IRQ:' in text: raise RuntimeError(text[-2000:])
            if (marker and marker in text) or (not marker and re.search(r'[\w.-]+@[\w.-]+:/[^\n]* $',text)):return text
        raise TimeoutError(text[-2000:])
    def command(self,line,timeout=30):
        if len(line)>191: raise ValueError('Shell line too long')
        self.c.write((line+'\r').encode());return self.read(timeout=timeout)
    def python(self,source,timeout=30):
        # input supports 4096 bytes, while the transport queue holds 256 bytes.
        self.c.write(b'python -c "exec(input(\'BOOT>\'))"\r')
        self.read('\nBOOT>')
        # Allow the worker to settle before sending a long input line.
        time.sleep(.15);self.c.read(8192)
        raw=(source+'\r').encode()
        for i in range(0,len(raw),128):
            self.c.write(raw[i:i+128]);time.sleep(.02)
        return self.read(timeout=timeout)
    def install(self,source,target,replace=False):
        data=source.read_bytes();expected=hashlib.sha256(data).hexdigest()
        # Streaming hashing does not need the entire library/archive in VM RAM.
        probe="import hashlib,binascii;exec(\"try:\\n f=open(%r,'rb');h=hashlib.sha256()\\n while True:\\n  b=f.read(1024)\\n  if not b:break\\n  h.update(b)\\n f.close();print('HASH='+binascii.hexlify(h.digest()).decode())\\nexcept OSError:print('MISSING')\")" % target
        out=self.python(probe)
        if 'HASH='+expected in out:
            print('unchanged',target,flush=True);return
        existing=bool(re.search(r'HASH=[0-9a-f]{64}',out))
        if existing and not replace:raise RuntimeError('Preserving different existing file '+target)
        if 'Traceback' in out:raise RuntimeError(out[-2000:])
        temp=target+'.part'
        bootstrap=("import binascii,hashlib;f=open(%r,'wb');h=hashlib.sha256();print('READY!');exec(\"while True:\\n s=input()\\n if s=='END!':break\\n b=binascii.a2b_base64(s);f.write(b);h.update(b);print('ACK!')\");f.close();print('HASH='+binascii.hexlify(h.digest()).decode())" % temp)
        self.c.write(b'python -c "exec(input(\'BOOT>\'))"\r');self.read('\nBOOT>');time.sleep(.15);self.c.read(8192)
        raw=(bootstrap+'\r').encode()
        for i in range(0,len(raw),128):self.c.write(raw[i:i+128]);time.sleep(.02)
        self.read('\nREADY!\n')
        # Consume any trailing output before the first encoded data block.
        time.sleep(.1);self.c.read(8192)
        for i in range(0,len(data),132):
            self.c.write(base64.b64encode(data[i:i+132])+b'\r');self.read('ACK!',timeout=15)
            if i and i%66000==0:print(target,i,'/',len(data),flush=True)
        self.c.write(b'END!\r');out=self.read()
        if 'HASH='+expected not in out:raise RuntimeError('Transfer hash mismatch '+target+' '+out[-500:])
        if existing:
            backup=target+'.bak'
            out=self.python('import _solaros_hw as h;h.fs(6,%r,%r);print("BACKED_UP")'%(target,backup))
            if '\nBACKED_UP\n' not in out:raise RuntimeError(out[-2000:])
        out=self.python('import _solaros_hw as h;h.fs(6,%r,%r);print("INSTALLED")'%(temp,target))
        if '\nINSTALLED\n' not in out and existing:
            self.python('import _solaros_hw as h;h.fs(6,%r,%r)'%(backup,target))
        if '\nINSTALLED\n' in out and existing:
            self.python('import _solaros_hw as h;h.fs(4,%r)'%backup)
        if '\nINSTALLED\n' not in out:raise RuntimeError(out[-2000:])
        print('installed',target,len(data),expected,flush=True)
    def close(self):self.c.close()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--replace',action='store_true')
    p.add_argument('--archive',type=Path)
    args=p.parse_args();board=Board()
    try:
        board.command('mkdir /flash/lib')
        for source in sorted((Path(__file__).resolve().parents[2]/'lib/teensy41').iterdir()):
            if source.is_file():board.install(source,'/flash/lib/'+source.name,args.replace)
        board.command('mkdir /sd/python-offline')
        if args.archive:board.install(args.archive,'/sd/python-offline/'+args.archive.name,args.replace)
        board.command('mkdir /sd/python-examples')
        for source in sorted((Path(__file__).resolve().parents[2]/'examples/teensy41/python').glob('*.py')):
            board.install(source,'/sd/python-examples/'+source.name,args.replace)
    finally:board.close()
if __name__=='__main__':main()
