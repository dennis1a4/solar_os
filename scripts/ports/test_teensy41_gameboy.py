#!/usr/bin/env python3
"""Live small-display Game Boy smoke test using an installed homebrew ROM."""
import argparse,json,re,time,ftplib
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_ftp import Console
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--rom',default='/sd/roms/2048.gb')
parser.add_argument('--log',type=Path,required=True)
parser.add_argument('--controls-only',action='store_true')
parser.add_argument('--leave-running',action='store_true')
args=parser.parse_args()
r={'commands':[],'fps':[],'passed':False}
port=next(p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x16c0,0x0483))
with serial.Serial(port,115200,timeout=.03,write_timeout=3,exclusive=True) as usb:
 c=Console(usb,r);active=False;ftp_started=False
 def mem():
  out=c.command('mem')
  return {k:int(v) for k,v in re.findall(r'(DTCM|OCRAM|PSRAM) \([^\n]+?\): used=\d+ free=(\d+)',out)}
 try:
  c.command('');before=mem()
  if not args.controls_only: assert 'frames=120' in c.command('lcd small benchmark')
  time.sleep(2)
  for run in range(0 if args.controls_only else 2):
   c.command('lcd send "gameboy '+args.rom+'"');active=True
   time.sleep(4)
   out=c.command('sessions');assert re.search(r'active\s+gameboy',out),out
   c.command('lcd key enter')
   for _ in range(3):
    time.sleep(3)
    out=c.command('lcd small gameboy');print(out,flush=True)
    fps=re.search(r'emu_fps=([\d.]+) present_fps=([\d.]+)',out);assert fps,out
    emu,present=map(float,fps.groups());r['fps'].append([emu,present]);assert emu>50 and present>24,(emu,present)
    assert 'GB_USB_ALIVE' in c.command('echo GB_USB_ALIVE')
    c.command('lcd key left');c.command('lcd key down')
   if run==0:
    out=c.command('network status');assert 'link=up' in out,out
    host=re.search(r'address=(\d+\.\d+\.\d+\.\d+)',out)[1]
    assert 'ftpd stopped' in c.command('job status ftpd')
    assert 'ftpd: OK' in c.command('job start ftpd /sd/roms 2121 --user test --password gb-test');ftp_started=True
    with ftplib.FTP() as ftp:
     ftp.connect(host,2121,timeout=15);ftp.login('test','gb-test');data=bytearray();ftp.retrbinary('RETR '+Path(args.rom).name,data.extend)
     assert len(data)>=32768
    c.command('job stop ftpd');ftp_started=False
   updates=int(re.search(r'updates=(\d+)',c.command('lcd small'))[1])
   c.command('lcd key exit');active=False;time.sleep(3)
   out=c.command('lcd small');assert int(re.search(r'updates=(\d+)',out)[1])>updates,out
   after=mem();r.setdefault('memory',[]).append(after)
   assert after['OCRAM']==before['OCRAM'] and after['PSRAM']>=before['PSRAM']-4096,(before,after)
  for path,detail in (('/sd/roms/not-present.gb','file not found'),('/sd/roms/2048-LICENSE.txt','ROM')):
   c.command('lcd send "gameboy '+path+'"');time.sleep(2)
   out=c.command('sessions');assert not re.search(r'active\s+gameboy',out),out
   out=c.command('lcd dump');assert 'gameboy:' in out,out
  c.command('lcd send "gameboy '+args.rom+'"');active=True;time.sleep(4)
  c.command('lcd key p');time.sleep(.3)
  paused=c.command('lcd small gameboy');time.sleep(3)
  assert c.command('lcd small gameboy')==paused
  c.command('lcd key p');time.sleep(5)
  assert 'emu_fps=59.' in c.command('lcd small gameboy')
  c.command('lcd key r');time.sleep(5)
  assert 'emu_fps=59.' in c.command('lcd small gameboy')
  assert 'busy' in c.command('lcd small benchmark').lower()
  if args.leave_running:
   active=False;r['left_running']=True
  else:
   c.command('lcd key exit');active=False;time.sleep(2)
  r['passed']=True
 finally:
  try:
   if ftp_started:c.command('job stop ftpd')
   if active:c.command('lcd key exit')
  finally:args.log.write_text(json.dumps(r,indent=2)+'\n')
print('PASS: Game Boy controls, invalid ROMs and display ownership' if args.controls_only else
      'PASS: Game Boy FPS, USB/FTP coexistence, controls, dashboard recovery and repeated lifecycle cleanup')
