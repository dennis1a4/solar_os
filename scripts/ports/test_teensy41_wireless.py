import sys,time,json
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import serial
from serial.tools import list_ports
from test_teensy41_ftp import Console
report={'passed':False,'commands':[],'checks':[]}
import argparse
parser=argparse.ArgumentParser(description='Live Teensy checks with SPI bridge unwired.')
parser.add_argument('--log',type=Path,required=True)
out=parser.parse_args().log
try:
 ports=[p.device for p in list_ports.comports() if p.vid==0x16c0 and p.pid==0x0483]
 assert len(ports)==1,ports
 with serial.Serial(ports[0],115200,timeout=.03,write_timeout=3,exclusive=True) as s:
  c=Console(s,report);c.command('')
  assert 'bridge off' in c.command('wbridge status')
  before=c.command('io claims')
  for i in range(3):
   started=time.monotonic();r=c.command('wbridge on');elapsed=time.monotonic()-started
   assert 'link/device error' in r,r
   assert elapsed<3,elapsed
   assert 'bridge off' in c.command('wbridge status')
   claims=c.command('io claims');assert 'wireless:slot0' not in claims,claims
  report['checks'].append('three absent-SPI attempts time out and release pins')
  assert 'not ready' in c.command('wifi scan')
  assert 'not ready' in c.command('bt scan')
  assert 'not ready' in c.command('bt output bluetooth')
  c.command('bt output local');c.command('wbridge off')
  report['checks'].append('unavailable Wi-Fi/BT operations fail cleanly')
  assert 'failures=0' in c.command('mem')
  assert 'eth0:' in c.command('network status')
  assert 'LCD 100x30' in c.command('lcd status')
  assert 'unknown command' not in c.command('uptime')
  report['checks'].append('shell, memory, network and display status remain responsive')
  report['passed']=True
finally:
 out.write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps({'passed':report['passed'],'checks':report['checks']},indent=2))
