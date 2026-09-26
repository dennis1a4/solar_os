#!/usr/bin/env python3
"""Test Teensy Python TCP clients against a temporary local fixture server.
Creates unique SD files. No LAN scan; server binds only the chosen LAN address.
"""
import argparse
import base64
import hashlib
import json
import socket
import socketserver
import threading
import time
import uuid
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_shell import ANSI, PROMPT

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--board-ip', default='192.168.1.197')
parser.add_argument('--log', type=Path, required=True)
parser.add_argument('--public', action='store_true', help='Also fetch example.com and install /http_fetch.py if absent')
parser.add_argument('--cable', action='store_true', help='Interactive unplug/replug test instead of the main suite')
a = parser.parse_args()
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as route:
    route.connect((a.board_ip, 9))
    host = route.getsockname()[0]
body = b'SolarOS Ethernet Python test\n' + bytes(range(256))*16
response = b'HTTP/1.0 200 OK\r\nContent-Length: '+str(len(body)).encode()+b'\r\nConnection: close\r\n\r\n'+body
shutdown = threading.Event()
class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(10)
        try:
            data = b''
            while b'\r\n\r\n' not in data and len(data)<4096:
                chunk = self.request.recv(512)
                if not chunk: return
                data += chunk
            if b'GET /stall ' in data:
                shutdown.wait(180)
            elif b'POST /echo ' in data:
                headers, payload = data.split(b'\r\n\r\n', 1)
                length = int(next(line.split(b':',1)[1] for line in headers.split(b'\r\n') if line.startswith(b'Content-Length:')))
                while len(payload)<length:
                    chunk=self.request.recv(1024)
                    if not chunk: return
                    payload += chunk
                self.request.sendall(payload[:length])
            else:
                self.request.sendall(response)
        except (OSError, TimeoutError):
            pass
class Server(socketserver.ThreadingTCPServer):
    daemon_threads = True
server = Server((host, 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
port = server.server_address[1]
root = '/_solaros_pynet_'+uuid.uuid4().hex[:10]
r = {'passed': False, 'directory': root, 'commands': []}
try:
    ports = [p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x16c0,0x0483)]
    assert len(ports)==1, ports
    with serial.Serial(ports[0],115200,timeout=.02,write_timeout=3,exclusive=True) as conn:
        time.sleep(1); conn.reset_input_buffer()
        def exchange(raw, suffix=PROMPT, expected=None, timeout=20):
            if raw: conn.write(raw)
            data = bytearray(); deadline = time.monotonic()+timeout
            while time.monotonic()<deadline:
                data.extend(conn.read(8192))
                text = ANSI.sub('',data.decode(errors='replace')).replace('\r','')
                if text.endswith(suffix):
                    r['commands'].append({'input':raw.decode(errors='replace'),'output':text})
                    if expected is not None: assert expected in text, text
                    return text
            raise RuntimeError(f'Timeout: {data[-1000:]!r}')
        def cmd(s, expected=None): return exchange((s+'\r').encode(), expected=expected)
        def py(s, expected=None):
            text=exchange((s+'\r').encode(),suffix='>>> ',expected=expected)
            assert 'Traceback' not in text, text
            return text
        def block(s): return py('exec('+repr(s)+')')
        conn.write(b'\x03'); time.sleep(.15)
        initial=conn.read(8192).decode(errors='replace')
        if initial.endswith('>>> '): exchange(b'\x04')
        cmd(''); cmd('network up','started')
        deadline=time.monotonic()+30
        while 'DHCP=bound' not in cmd('network status'):
            assert time.monotonic()<deadline; time.sleep(1)
        if a.cable:
            exchange(b'python\r',suffix='>>> ')
            py('import socket; s=socket.socket(); s.connect(('+repr(host)+','+str(port)+')); s.settimeout(None)')
            py("s.sendall(b'GET /stall HTTP/1.0\\r\\n\\r\\n')")
            conn.write(b's.recv(1)\r')
            print('READY: blocked receive; unplug only the Ethernet cable now.',flush=True)
            exchange(b'',suffix='>>> ',expected='OSError',timeout=150)
            py('s.close()'); exchange(b'\x04')
            r['disconnected']=cmd('network status','link=down')
            print('PASS: blocked receive returned on link loss. Reconnect Ethernet now.',flush=True)
            deadline=time.monotonic()+150
            while 'link=up' not in (status:=cmd('network status')) or 'DHCP=bound' not in status:
                assert time.monotonic()<deadline
                time.sleep(1)
            exchange(b'python\r',suffix='>>> ')
            py('import socket, hashlib, binascii; s=socket.socket(); s.connect(('+repr(host)+','+str(port)+'))')
            py("s.sendall(b'GET /data HTTP/1.0\\r\\n\\r\\n')")
            block("response=bytearray()\nwhile True:\n chunk=s.recv(512)\n if not chunk: break\n response.extend(chunk)\ns.close()")
            py('print(binascii.hexlify(hashlib.sha256(response).digest()).decode())',hashlib.sha256(response).hexdigest())
            exchange(b'\x04')
            r['status']=cmd('network status'); r['passed']=True
            print('PASS: cable-loss exception and exact HTTP transfer after reconnection',flush=True)
        else:
            cmd('mkdir '+root)
            exchange(b'python\r',suffix='>>> ')
            py('import socket, gc, binascii, hashlib, errno')
            py("print(socket.getaddrinfo('example.com',80))", "[(2, 1, 6,")
            py("f=open('"+root+"/http_fetch.py','xb')")
            for offset in range(0,len(script := Path('examples/teensy41/http_fetch.py').read_bytes()),384):
                py('f.write(binascii.a2b_base64('+repr(base64.b64encode(script[offset:offset+384]).decode())+'))')
            py('f.close()')
            # Constructor limits, explicit close, GC finalizers, and descriptor reuse.
            block("socks=[socket.socket() for _ in range(4)]\ntry:\n socket.socket()\n raise AssertionError('socket limit')\nexcept OSError as e:\n assert e.args[0]==24\nfor s in socks: s.close()\nsocks=[]\ns=None\ngc.collect()\nfor i in range(20):\n s=socket.socket()\n s=None\n gc.collect()\nprint('cleanup OK')")
            # Exercise copied payloads spanning multiple worker requests.
            py('payload=bytes(range(256))*16; s=socket.socket(); s.connect(('+repr(host)+','+str(port)+'))')
            py("s.sendall(b'POST /echo HTTP/1.0\\r\\nContent-Length: 4096\\r\\n\\r\\n'); s.sendall(payload)")
            block("received=bytearray()\nwhile True:\n part=s.recv(4096)\n if not part: break\n received.extend(part)\nassert received==payload\ns.close()\nprint('large send OK')")
            # No listener at this reserved local endpoint: connect must fail and free its slot.
            with socket.socket() as closed_endpoint:
                closed_endpoint.bind((host,0))
                refused_port=closed_endpoint.getsockname()[1]
                block("s=socket.socket()\ns.settimeout(.5)\ntry:\n s.connect(("+repr(host)+","+str(refused_port)+"))\n raise AssertionError('connect should fail')\nexcept OSError:\n pass\ns.close()")
            py('s=socket.socket(); s.close(); s.close()')
            block("try:\n s.recv(1)\n raise AssertionError('closed socket')\nexcept OSError as e:\n assert e.args[0]==errno.EBADF")
            py('s=socket.socket(); s.settimeout(.15); s.connect(('+repr(host)+','+str(port)+'))')
            py("s.sendall(b'GET /stall HTTP/1.0\\r\\n\\r\\n')")
            begin=time.monotonic()
            block("try:\n s.recv(1)\n raise AssertionError('timeout')\nexcept OSError as e:\n assert e.args[0]==errno.ETIMEDOUT\nprint('timeout OK')")
            assert time.monotonic()-begin<2
            py('s.settimeout(0)')
            block("try:\n s.recv(1)\n raise AssertionError('nonblocking')\nexcept OSError as e:\n assert e.args[0]==errno.EAGAIN")
            py('s.settimeout(None)')
            conn.write(b's.recv(1)\r'); time.sleep(.25)
            exchange(b'\x03',suffix='>>> ',expected='KeyboardInterrupt')
            py('s.close(); socks=[socket.socket() for _ in range(4)]')
            py("print('slots recovered',len(socks))",'slots recovered 4')
            # Exit with live sockets: the next interpreter must recover all slots.
            exchange(b'\x04')
            exchange(b'python\r',suffix='>>> ')
            py('import socket; socks=[socket.socket() for _ in range(4)]')
            exchange(b'\x04')
            destination=root+'/response.bin'
            line='python '+root+'/http_fetch.py '+host+' '+str(port)+' /data '+destination
            cmd(line,'Saved '+str(len(response))+' bytes')
            # Exclusive creation must preserve the first file.
            cmd(line,'OSError')
            exchange(b'python\r',suffix='>>> ')
            py('import hashlib, binascii')
            py("f=open('"+destination+"','rb'); print(binascii.hexlify(hashlib.sha256(f.read()).digest()).decode()); f.close()",hashlib.sha256(response).hexdigest())
            exchange(b'\x04')
            r['memory_before']=cmd('mem')
            for i in range(5): cmd(line.rsplit(' ',1)[0]+' '+root+'/repeat'+str(i)+'.bin','Saved '+str(len(response))+' bytes')
            r['memory_after']=cmd('mem')
            assert r['memory_before']==r['memory_after']
            cmd('network down','stopped')
            cmd(line.rsplit(' ',1)[0]+' '+root+'/offline.bin','OSError')
            cmd('network up','started')
            deadline=time.monotonic()+30
            while 'DHCP=bound' not in cmd('network status'):
                assert time.monotonic()<deadline; time.sleep(1)
            cmd(line.rsplit(' ',1)[0]+' '+root+'/recovered.bin','Saved '+str(len(response))+' bytes')
            if a.public:
                cmd('python '+root+'/http_fetch.py example.com 80 / '+root+'/public-response.txt','Saved ')
                exchange(b'python\r',suffix='>>> ')
                py("f=open('"+root+"/public-response.txt','rb'); print(f.readline()); f.close()",'HTTP/')
                exchange(b'\x04')
                r['example_install']=cmd('cp '+root+'/http_fetch.py /http_fetch.py')
            r['status']=cmd('network status'); r['uptime']=cmd('uptime')
            r['passed']=True
            print('PASS: Python DNS/TCP, exact HTTP-to-SD bytes, timeout, Ctrl-C, GC/session cleanup and restart')
            print(root)

finally:
    shutdown.set(); server.shutdown(); server.server_close()
    a.log.write_text(json.dumps(r,indent=2)+'\n')
