#!/usr/bin/env python3
"""Copy a pinned library archive to SD using a temporary LAN HTTP server.
Only the selected archive is served; verify SHA256 before publishing the copy.
"""
import argparse,hashlib,http.server,re,socket,threading,time
from pathlib import Path
from install_teensy41_python_bundle import Board
p=argparse.ArgumentParser(description=__doc__);p.add_argument('archive',type=Path);a=p.parse_args()
payload=a.archive.read_bytes();digest=hashlib.sha256(payload).hexdigest()
class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path!='/archive':self.send_error(404);return
        self.send_response(200);self.send_header('Content-Length',str(len(payload)));self.end_headers();self.wfile.write(payload)
    def log_message(self,*args):pass
board=Board();was_up=True;server=None
try:
    state=board.command('network status');was_up='eth0: stopped' not in state
    if not was_up:board.command('network up')
    until=time.monotonic()+30
    while True:
        state=board.command('network status');m=re.search(r'address=(\d+\.\d+\.\d+\.\d+)',state)
        if m and m[1]!='0.0.0.0' and 'link=up' in state:break
        if time.monotonic()>until:raise RuntimeError(state)
        time.sleep(.5)
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as route:
        route.connect((m[1],9));host=route.getsockname()[0]
    server=http.server.ThreadingHTTPServer((host,0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start()
    board.command('mkdir /sd/python-offline')
    target='/sd/python-offline/'+a.archive.name
    # Create a new temporary file, then rename only after an exact hash match.
    source="""import socket,hashlib,binascii,_solaros_hw as hw
s=socket.socket();s.settimeout(10);s.connect((%r,%d));s.sendall(b'GET /archive HTTP/1.0\\r\\n\\r\\n')
header=b''
while b'\\r\\n\\r\\n' not in header:
 header+=s.recv(1)
 if len(header)>2048:raise ValueError('bad HTTP header')
if not header.startswith(b'HTTP/1.0 200'):raise ValueError(header)
f=open(%r,'wb');h=hashlib.sha256();total=0
while True:
 chunk=s.recv(4096)
 if not chunk:break
 total+=len(chunk)
 if total>%d:raise ValueError('archive too large')
 f.write(chunk);h.update(chunk)
f.close();s.close()
if total!=%d or binascii.hexlify(h.digest()).decode()!=%r:raise ValueError('archive hash mismatch')
hw.fs(6,%r,%r);print('ARCHIVE_VERIFIED',total)
"""%(host,server.server_port,target+'.part',len(payload),len(payload),digest,target+'.part',target)
    result=board.python('exec('+repr(source)+')',timeout=180)
    print(result[-2500:]);assert '\nARCHIVE_VERIFIED ' in result,result[-1500:]
    print(target,digest,flush=True)
finally:
    if server:server.shutdown();server.server_close()
    if not was_up:board.command('network down')
    board.close()
