#!/usr/bin/env python3
"""Interactive recorder acceptance on an idle Teensy with SD and audio shield.
Uses a unique fixture directory; leaves user recordings untouched.
"""
import argparse
import io
import json
import re
import time
import uuid
import wave
from pathlib import Path
from teensy41_audio_probe import Console

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--log',type=Path,required=True)
a=p.parse_args()
r={'passed':False}
c=Console(r)
root='/sd/_recorder_'+uuid.uuid4().hex[:8]
active=False
retained=None

def key(k):
    assert 'Queued' in c.cmd('lcd key '+k)
    time.sleep(.35)

def screen(): return c.cmd('lcd dump')

def start(path):
    global active
    assert 'Queued' in c.cmd('lcd send '+json.dumps('recorder '+path))
    active=True;time.sleep(.6)
    out=screen();assert 'Recorder' in out and '44100 Hz, mono, 16 bit' in out,out

def stop():
    global active
    key('exit');active=False
    out=c.cmd('audio status')
    assert 'playback=0 capture=0 bytes' in out,out
    assert 'allocated=0' in out and 'running=0' in out,out

try:
    assert 'SGTL5000=ready' in c.cmd('audio status')
    assert 'recorder' in c.cmd('apps')
    c.cmd('mkdir '+root)
    # Warm settings/browser allocations before comparing repeated lifecycles.
    start(root+'/warm.wav');stop();baseline=c.memory()
    start(root+'/take.wav')
    key('r');time.sleep(1.2)
    assert 'recording' in screen()
    key('space');paused=screen();time.sleep(1.3)
    after=screen()
    assert 'paused' in paused and 'paused' in after,paused
    assert re.search(r'paused\s+(\d+:\d+)',paused)[1]==re.search(r'paused\s+(\d+:\d+)',after)[1]
    key('space');time.sleep(.8)
    # Retained recorder keeps capturing while its console returns to the shell.
    key('ctrlz');out=c.cmd('sessions');r['background_sessions']=out
    assert 'recorder' in out,out
    match=re.search(r'^\s*(\d+)\s+.*recorder',out,re.M);assert match,out
    retained=match[1]
    time.sleep(1)
    assert 'in use' in c.cmd('player '+root).lower()
    # Remote close must join the worker and finalize its WAV before freeing UI.
    match=re.search(r'^\s*(\d+)\s+.*recorder',out,re.M);assert match,out
    c.cmd('close '+match[1]);time.sleep(.7);active=False;retained=None
    out=c.cmd('audio status');assert 'overruns=0' in out,out
    assert 'playback=0 capture=0 bytes' in out,out
    data=c.download(root+'/take.wav')
    with wave.open(io.BytesIO(data),'rb') as f:
        r['wav']={'channels':f.getnchannels(),'rate':f.getframerate(),
                  'width':f.getsampwidth(),'frames':f.getnframes()}
        assert (f.getnchannels(),f.getframerate(),f.getsampwidth())==(1,44100,2)
        assert f.getnframes()>44100*2
    assert int.from_bytes(data[40:44],'little')==len(data)-44
    # Existing recording cannot be overwritten; playback and monitoring work.
    start(root+'/take.wav');key('r');assert 'failed' in screen().lower()
    key('p');time.sleep(.4);assert 'playing' in screen()
    key('space');assert 'paused' in screen();key('s')
    key('m');time.sleep(.6);assert 'monitoring' in screen();key('m')
    stop()
    assert c.download(root+'/take.wav')==data
    for n in range(3):
        start(root+'/cycle%d.wav'%n);key('r');time.sleep(.4);key('s');stop()
        assert c.memory()==baseline,(baseline,c.memory())
        c.cmd('rm '+root+'/cycle%d.wav'%n)
    c.cmd('rm '+root+'/take.wav');c.cmd('rm -r '+root)
    r['memory']=c.memory();r['status']=c.cmd('audio status');r['passed']=True
finally:
    if retained:
        try:c.cmd('close '+retained);time.sleep(.5);active=False
        except Exception as e:r['cleanup_error']=str(e)
    if active:
        try:stop()
        except Exception as e:r['cleanup_error']=str(e)
    c.close();a.log.write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps({k:v for k,v in r.items() if k!='commands'},indent=2))
