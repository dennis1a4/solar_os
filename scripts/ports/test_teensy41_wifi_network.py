#!/usr/bin/env python3
"""Verify exact TCP/UDP data over an already-connected SolarOS Wi-Fi bridge.
Requires Ethernet stopped (network down). Leaves Wi-Fi and routing unchanged.
Runs temporary echo fixtures bound to this computer's route toward the board.
"""
import sys,socket,socketserver,threading,serial,json,time,re,argparse
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_teensy41_ftp import Console
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--board-ip',required=True)
parser.add_argument('--port',required=True)
parser.add_argument('--log',type=Path,required=True)
a=parser.parse_args()
r={'commands':[],'peers':[],'passed':False}
with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as route:
 route.connect((a.board_ip,9));host=route.getsockname()[0]
class TCP(socketserver.BaseRequestHandler):
 def handle(self):
  r['peers'].append(self.client_address[0]);self.request.settimeout(15)
  data=b''
  while len(data)<16384:
   part=self.request.recv(16384-len(data))
   if not part:return
   data+=part
  self.request.sendall(data)
class UDP(socketserver.BaseRequestHandler):
 def handle(self):
  data,sock=self.request;r['peers'].append(self.client_address[0]);sock.sendto(data,self.client_address)
tcp=socketserver.ThreadingTCPServer((host,0),TCP);udp=socketserver.ThreadingUDPServer((host,0),UDP)
for server in (tcp,udp):threading.Thread(target=server.serve_forever,daemon=True).start()
try:
 with serial.Serial(a.port,115200,timeout=.03,write_timeout=3,exclusive=True) as s:
  c=Console(s,r);c.command('')
  status=c.command('network status');assert 'eth0: stopped' in status and 'default via wifi0' in status,status
  c.command('python',python=True)
  def py(code):
   out=c.command(code,python=True);assert not any(x in out for x in ['Traceback','Error:']),out
   return out
  py('import socket, solaros; n=solaros.net')
  code="s=socket.socket()\ns.settimeout(15)\ns.connect(("+repr(host)+","+str(tcp.server_address[1])+"))\npayload=bytes(range(256))*64\ns.sendall(payload)\nreceived=bytearray()\nwhile len(received)<len(payload):\n part=s.recv(4096)\n if not part: break\n received.extend(part)\nassert received==payload\ns.close()\nprint('TCP 16384 exact bytes OK')"
  print(py('exec('+repr(code)+')'),flush=True)
  code="u=n.udp_open()\nfor size in (1,512,1400,4096):\n data=bytes(range(256))*(size//256)+b'x'*(size%256)\n n.udp_send(u,"+repr(host)+","+str(udp.server_address[1])+",data,5000)\n result=n.udp_receive(u,4096,5000)\n assert result is not None and result['data']==data and not result['truncated']\nn.close(u)\nprint('UDP 1/512/1400/4096 exact bytes OK')"
  print(py('exec('+repr(code)+')'),flush=True)
  s.write(b'\x04');time.sleep(.4);s.read_all()
  for cmd in ['wifi status','wbridge status','mem']:
   print(c.command(cmd),flush=True)
  assert r['peers'] and all(x==a.board_ip for x in r['peers']),r['peers']
  r['passed']=True
finally:
 for server in (tcp,udp):server.shutdown();server.server_close()
 a.log.write_text(json.dumps(r,indent=2)+'\n')
