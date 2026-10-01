#!/usr/bin/env python3
"""Plot retained-session hardware check. Requires exclusive USB and idle keyboard."""
import argparse,time,json,re
from pathlib import Path

import serial
from serial.tools import list_ports
from test_teensy41_hotplug import Console,require
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--log',type=Path,required=True)
args=parser.parse_args()
ports=[p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x16c0,0x0483)]
assert len(ports)==1
s=serial.Serial(ports[0],115200,timeout=.05,exclusive=True)
report={'commands':[],'passed':False}
c=Console(s,report)
try:
 time.sleep(1);c.command('\x1d');c.command('lcd send "plot uptime --rate 250"');time.sleep(3)
 out=c.command('sessions');m=re.search(r'^(\d+)\s+lcd-shell\s+active\s+plot',out,re.M);assert m,out
 sid=m[1];assert 'graphics=active' in c.command('lcd')
 c.command('lcd key ctrlz');time.sleep(1);assert 'graphics=idle' in c.command('lcd')
 assert re.search(r'^'+sid+r'\s+lcd-shell\s+suspended\s+plot',c.command('sessions'),re.M)
 c.command('lcd send "calc"');time.sleep(.5);c.command('lcd key ctrlz')
 c.command('fg '+sid);time.sleep(2);out=c.command('lcd');assert 'graphics=active' in out,out
 frames=int(re.search(r'frames=(\d+)',out)[1]);time.sleep(1)
 assert int(re.search(r'frames=(\d+)',c.command('lcd'))[1])>frames
 c.command('close '+sid);time.sleep(.5);assert 'graphics=idle' in c.command('lcd')
 out=c.command('sessions');m=re.search(r'^(\d+)\s+lcd-shell\s+suspended\s+calc',out,re.M);assert m,out
 c.command('close '+m[1]);time.sleep(.5)
 assert not re.search(r'^\d+\s+lcd-shell\s+(active|suspended)',c.command('sessions'),re.M)
 report['passed']=True;print('PASS: Plot suspend/text switch/resume, continuing frame output, close and retained Calc cleanup')
finally:
 s.close();args.log.write_text(json.dumps(report,indent=2)+'\n')
