#!/usr/bin/env python3
"""SSH hardware regression against an isolated password-auth test server.
Requires paramiko and pyserial. No host shell commands or system SSH changes.
Uses a random port; SolarOS retains its host key in the existing known_hosts.
"""
import argparse
import json
import socket
import threading
import time
import uuid
from pathlib import Path
import paramiko
import serial
from serial.tools import list_ports
from test_teensy41_shell import ANSI, PROMPT

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--board-ip',default='192.168.1.197')
p.add_argument('--log',type=Path,required=True)
p.add_argument('--smoke',action='store_true')
a=p.parse_args()
with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as route:
    route.connect((a.board_ip,9)); host=route.getsockname()[0]
password=uuid.uuid4().hex
key=paramiko.ECDSAKey.generate()
current_key=[key]
silent=[False]
halt=threading.Event()
transports=[]
bulk='0123456789abcdef'*256
report={'passed':False,'commands':[],'server_errors':[]}

class Server(paramiko.ServerInterface):
    def __init__(self): self.shell=threading.Event()
    def check_auth_password(self,user,secret):
        return paramiko.AUTH_SUCCESSFUL if user=='fixture' and secret==password else paramiko.AUTH_FAILED
    def get_allowed_auths(self,user): return 'password'
    def check_channel_request(self,kind,chanid):
        return paramiko.OPEN_SUCCEEDED if kind=='session' else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED
    def check_channel_pty_request(self,*args): return True
    def check_channel_env_request(self,*args): return True
    def check_channel_shell_request(self,channel): self.shell.set(); return True

def serve_connection(conn):
    if silent[0]:
        conn.settimeout(.2)
        try:
            while not halt.is_set():
                try:
                    if not conn.recv(256): break
                except socket.timeout: continue
        finally: conn.close()
        return
    transport=paramiko.Transport(conn); transports.append(transport)
    transport.add_server_key(current_key[0]); server=Server()
    try:
        transport.start_server(server=server)
        channel=transport.accept(25)
        if channel is None or not server.shell.wait(10): return
        channel.settimeout(.5); channel.sendall(b'fixture ready\r\n$ ')
        line=bytearray()
        while transport.is_active() and not halt.is_set():
            try: data=channel.recv(256)
            except socket.timeout: continue
            if not data: break
            for ch in data:
                if ch==13:
                    text=line.decode(errors='replace'); line.clear()
                    if text=='exit':
                        channel.send_exit_status(0); channel.shutdown_write(); channel.close()
                        time.sleep(.2); return
                    if text=='drop': return
                    answer=bulk if text=='bulk' else 'hello from SSH' if text=='hello' else 'received: '+text
                    channel.sendall(('\r\n'+answer+'\r\n$ ').encode())
                elif ch==3: line.clear(); channel.sendall(b'^C\r\n$ ')
                elif ch==127:
                    if line: line.pop()
                else: line.append(ch)
    except (EOFError,OSError,paramiko.SSHException) as exc:
        report['server_errors'].append(str(exc))
    finally: transport.close()

listener=socket.socket(); listener.bind((host,0)); listener.listen(); listener.settimeout(.2)
port=listener.getsockname()[1]
def accept_loop():
    while not halt.is_set():
        try: conn,_=listener.accept()
        except socket.timeout: continue
        except OSError: break
        threading.Thread(target=serve_connection,args=(conn,),daemon=True).start()
threading.Thread(target=accept_loop,daemon=True).start()
report.update(host=host,port=port)
try:
    ports=[q.device for q in list_ports.comports() if (q.vid,q.pid)==(0x16c0,0x0483)]
    assert len(ports)==1,ports
    with serial.Serial(ports[0],115200,timeout=.02,write_timeout=3,exclusive=True) as conn:
        time.sleep(1); conn.reset_input_buffer()
        def exchange(raw=b'',suffix=PROMPT,contains=None,timeout=60,secret=False):
            if raw: conn.write(raw)
            data=bytearray(); deadline=time.monotonic()+timeout
            while time.monotonic()<deadline:
                data.extend(conn.read(8192))
                text=ANSI.sub('',data.decode(errors='replace')).replace('\r','')
                if 'Fault IRQ:' in text or 'Assertion failed' in text: break
                if suffix!=PROMPT and text.endswith(PROMPT): break
                if text.endswith(suffix):
                    report['commands'].append({'input':'<test password>' if secret else repr(raw),'output':text})
                    a.log.write_text(json.dumps(report,indent=2)+'\n')
                    if contains is not None: assert contains in text,text
                    return text
            report['commands'].append({'input':'<test password>' if secret else repr(raw),'output':text})
            raise RuntimeError('Timeout or firmware fault: '+text[-2500:])
        def cmd(s,expected=None): return exchange((s+'\r').encode(),contains=expected)
        def login(secret=password,expected='fixture ready',success=True):
            exchange(('ssh fixture@'+host+' '+str(port)+'\r').encode(),suffix=': ')
            return exchange((secret+'\r').encode(),suffix='$ ' if success else PROMPT,contains=expected,secret=True)
        conn.write(b'\x1d'); time.sleep(.2); conn.reset_input_buffer()
        cmd(''); cmd('cd /'); cmd('network up','started')
        deadline=time.monotonic()+30
        while 'DHCP=bound' not in cmd('network status'):
            assert time.monotonic()<deadline; time.sleep(1)
        cmd('apps','ssh - SSH client')
        report['before']=cmd('mem')
        login()
        exchange(b'hello\r',suffix='$ ',contains='hello from SSH')
        exchange(b'bulk\r',suffix='$ ',contains=bulk)
        exchange(b'bad\x7f\x7f\x7fhello\r',suffix='$ ',contains='hello from SSH')
        exchange(b'\x03',suffix='$ ',contains='^C')
        exchange(b'exit\r')
        report['after_first']=cmd('mem')
        if not a.smoke:
            login(secret='wrong',expected='password authentication failed',success=False)
            current_key[0]=paramiko.ECDSAKey.generate()
            login(expected='host key mismatch',success=False)
            current_key[0]=key
            for _ in range(5):
                login(); exchange(b'\x1d')
            login(); exchange(b'drop\r')
            # Cancellation during a stalled handshake, before authentication.
            silent[0]=True
            exchange(('ssh fixture@'+host+' '+str(port)+'\r').encode(),suffix=': ')
            exchange((password+'\r').encode(),suffix='ssh: starting SSH handshake\n',secret=True)
            begin=time.monotonic(); exchange(b'\x1d'); assert time.monotonic()-begin<3
            silent[0]=False
            login(); exchange(b'exit\r')
            report['after_repeats']=cmd('mem')
            assert report['after_first']==report['after_repeats'],report
        report['flash']=cmd('flash status','open=0')
        report['network']=cmd('network status','DHCP=bound')
        report['uptime']=cmd('uptime'); report['passed']=True
        print('PASS: SSH login, bidirectional I/O, terminal keys, close'+('' if a.smoke else ', auth/host-key rejection, cancellation and repeated cleanup'))
finally:
    halt.set(); listener.close()
    for transport in transports: transport.close()
    a.log.write_text(json.dumps(report,indent=2)+'\n')
