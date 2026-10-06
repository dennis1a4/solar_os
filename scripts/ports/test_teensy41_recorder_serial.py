#!/usr/bin/env python3
"""Recorder USB UI, monitored capture, setup and file-error acceptance."""
import argparse
import json
import re
import time
import uuid
from pathlib import Path
import pyte
from teensy41_audio_probe import Console

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--log',type=Path,required=True)
a=p.parse_args()
r={'passed':False};c=Console(r);active=False
path='/sd/_recorder_usb_'+uuid.uuid4().hex[:8]+'.wav'
dims=re.search(r'size (\d+) (\d+)',c.cmd('setterm'));assert dims
terminal=pyte.Screen(int(dims[1]),int(dims[2]));stream=pyte.Stream(terminal)
c.observe=lambda data:stream.feed(data.decode(errors='replace'))

def raw(data,seconds=.5):
    c.conn.write(data);end=time.monotonic()+seconds
    while time.monotonic()<end:
        chunk=c.conn.read(16384)
        assert not any(x in chunk for x in [b'Fault IRQ',b'STACK OVERFLOW',b'ASSERT in']),chunk
        c.observe(chunk)
    view='\n'.join(terminal.display)
    r.setdefault('keys',[]).append({'key':repr(data),'screen':view})
    return view

try:
    active=True
    assert 'Recorder' in raw(('recorder '+path+'\r').encode())
    # Gain setting persists, while format fields stay native.
    raw(b'\t'+b'\x1b[B'*6)
    assert 'Input gain' in raw(b'\x1b[C')
    raw(b'\x1b[D')
    raw(b'\t')
    assert 'recording' in raw(b'r',1.2)
    assert 'Recording monitor on' in raw(b'm',.8)
    assert 'paused' in raw(b' ',.7)
    raw(b' ',.7);raw(b'm');raw(b's')
    c.exchange(b'\x1d');active=False
    data=c.download(path)
    assert int.from_bytes(data[40:44],'little')==len(data)-44 and len(data)>88200
    c.cmd('rm '+path)
    active=True
    raw(b'recorder /sd/_recorder_missing_parent/take.wav\r')
    view=raw(b'r')
    deadline=time.monotonic()+3
    while 'failed' not in view and time.monotonic()<deadline:view=raw(b'',.2)
    assert 'failed' in view,view
    c.exchange(b'\x1d');active=False
    r['status']=c.cmd('audio status')
    assert 'playback=0 capture=0 bytes' in r['status']
    assert 'overruns=0' in r['status']
    r['passed']=True
finally:
    if active:
        try:c.exchange(b'\x1d')
        except Exception as e:r['cleanup_error']=str(e)
    c.close();a.log.write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps({'passed':r['passed'],'status':r.get('status')},indent=2))
