"""Small exclusive-console helper for audio acceptance scripts."""
import base64
import re
import time
import serial
from serial.tools import list_ports
from test_teensy41_shell import ANSI

class Console:
    def __init__(self, report):
        ports = [p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x16c0,0x0483)]
        assert len(ports)==1, ports
        self.conn=serial.Serial(ports[0],115200,timeout=.02,write_timeout=3,exclusive=True)
        self.report=report
        self.cmd('')
    def exchange(self, raw=b'', suffix=None, timeout=30, log=True):
        self.conn.write(raw);data=bytearray();last=time.monotonic();end=last+timeout
        while time.monotonic()<end:
            chunk=self.conn.read(16384)
            if chunk:data.extend(chunk);last=time.monotonic()
            text=ANSI.sub('',data.decode(errors='replace')).replace('\r','')
            assert 'Fault IRQ:' not in text and 'STACK OVERFLOW:' not in text,text
            found=text.endswith(suffix) if suffix else bool(re.search(r'[\w.-]+@[\w.-]+:/[^\n]* $',text))
            if found and time.monotonic()-last>.03:
                if log:self.report.setdefault('commands',[]).append({'input':repr(raw),'output':text})
                return text
        raise RuntimeError(text[-3000:])
    def cmd(self,s,timeout=30):return self.exchange((s+'\r').encode(),timeout=timeout)
    def memory(self):
        text=self.cmd('mem');return tuple(map(int,re.search(r'Internal heap: (\d+) free / \d+ bytes; PSRAM: (\d+) free',text).groups()))
    def download(self,path):
        self.exchange(b'python\r',suffix='>>> ')
        self.exchange(("import binascii;f=open(%r,'rb')\r"%path).encode(),suffix='>>> ')
        result=bytearray()
        while True:
            out=self.exchange(b"print('DATA:'+binascii.b2a_base64(f.read(1536)).decode().strip())\r",suffix='>>> ',log=False)
            m=re.search(r'^DATA:([A-Za-z0-9+/=]*)$',out,re.M);assert m,out[-1000:]
            block=base64.b64decode(m[1],validate=True)
            if not block:break
            result.extend(block);assert len(result)<5*1024*1024
        self.exchange(b'f.close()\r',suffix='>>> ');self.exchange(b'\x04')
        return result
    def close(self):self.conn.close()
