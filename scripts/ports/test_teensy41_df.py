#!/usr/bin/env python3
"""USB FAT32 df timings, maintained counts, refresh and bounded-read integrity.
Exclusive USB console. Uses one generated USB fixture, safe eject/remount and a
briefly suspended Python reader. Removes fixture after success; never formats.
"""
import argparse,hashlib,json,re,time,uuid
from pathlib import Path
from install_teensy41_python_bundle import Board
from teensy41_fixture_cleanup import cleanup_fixtures
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--log',type=Path,required=True)
a=p.parse_args();r={'passed':False,'commands':[]};b=Board();owned=None
root='/usb/_usb_test_'+uuid.uuid4().hex[:8]
def cmd(s):
 t=time.monotonic();out=b.command(s,timeout=180)
 r['commands'].append({'command':s,'seconds':time.monotonic()-t,'output':out});return out
def py(s):
 out=b.python('exec('+repr(s)+')',timeout=60)
 assert 'Traceback' not in out,out
 return out
def usage(refresh=False):
 out=cmd('df --refresh' if refresh else 'df')
 m=re.search(r'^/usb\s+(\d+)\s+(\d+)\s+(\d+)\s*$',out,re.M);assert m,out
 v=tuple(map(int,m.groups()));assert v[0]==v[1]+v[2];return v
def verify(size):
 expected=hashlib.sha256((bytes(range(256))*((size+255)//256))[:size]).hexdigest()
 out=py("import hashlib,binascii\nf=open(%r,'rb');h=hashlib.sha256()\nwhile True:\n d=f.read(16384)\n if not d:break\n h.update(d)\nf.close();print('HASH='+binascii.hexlify(h.digest()).decode())"%(root+'/data.bin'))
 assert 'HASH='+expected in out,out
try:
 assert 'FAT32' in cmd('usb status')
 assert 'safe to unplug' in cmd('usb eject');assert '/usb' in cmd('usb mount')
 cold=usage();r['cold_seconds']=r['commands'][-1]['seconds']
 assert usage()==cold;r['warm_seconds']=r['commands'][-1]['seconds']
 assert usage(True)==cold;r['refresh_seconds']=r['commands'][-1]['seconds']
 assert 'usage: df' in cmd('df --bad')
 assert 'mkdir:' not in cmd('mkdir '+root)
 before=usage()
 py("f=open(%r,'xb')\nfor i in range(513):f.write(bytes(range(256))*4)\nf.close()"%(root+'/data.bin'))
 after=usage();assert after[2]<before[2];assert usage(True)==after;verify(513*1024)
 # A live Python file prevents refresh; ordinary cached df remains usable.
 b.c.write(('python -c '+json.dumps("f=open(%r,'rb');input('HOLD>');f.close()"%(root+'/data.bin'))+'\r').encode())
 b.read('HOLD>');time.sleep(.2);b.c.write(b'\x1a');out=b.read()
 m=re.search(r'Suspended session (\d+)',out);assert m,out;owned=int(m[1])
 assert 'USB refresh busy' in cmd('df --refresh');assert usage()==after
 cmd('close '+str(owned));owned=None;time.sleep(.3)
 assert usage(True)==after;verify(513*1024)
 # Truncate, rename and delete must all agree with an independent recount.
 py("f=open(%r,'wb');f.write(bytes(range(256))*4);f.close()"%(root+'/data.bin'))
 small=usage();assert small[2]>after[2] and usage(True)==small;verify(1024)
 assert 'mv:' not in cmd('mv '+root+'/data.bin '+root+'/renamed.bin');assert usage()==small
 assert 'safe to unplug' in cmd('usb eject');assert '/usb' in cmd('usb mount');assert usage()==small
 assert 'rm:' not in cmd('rm '+root+'/renamed.bin');assert usage()==before and usage(True)==before
 cleanup_fixtures(cmd,[root],r)
 r['final_memory']=cmd('mem');r['final_usage']=usage();r['passed']=True
 print('PASS: USB df cold=%.3fs warm=%.3fs refresh=%.3fs; counts, live-handle refusal, data hashes, truncate/rename/delete/remount and fixture cleanup'%(r['cold_seconds'],r['warm_seconds'],r['refresh_seconds']))
finally:
 if owned:
  try:cmd('close '+str(owned))
  except Exception as e:r['cleanup_error']=str(e)
 b.close();a.log.write_text(json.dumps(r,indent=2)+'\n')
