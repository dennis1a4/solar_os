#!/usr/bin/env python3
"""Folder-player live acceptance; generated tones only, unique SD fixtures removed.
Requires exclusive USB, idle LCD and the audio shield. Tests transport counters;
a listening test is separate. Does not use or change a saved playlist.
"""
import argparse, hashlib, json, re, socket, subprocess, tempfile, threading, time, uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import quote, unquote
from pathlib import Path
import pyte
from teensy41_audio_probe import Console
from test_teensy41_shell import ANSI

p=argparse.ArgumentParser(description=__doc__);p.add_argument('--log',type=Path,required=True)
a=p.parse_args();r={'passed':False};root='/sd/_player_'+uuid.uuid4().hex[:8]
c=Console(r);active=False;usb_active=False;server=None
initial_ids=set(re.findall(r'^(\d+)\s+',c.cmd('sessions'),re.M))
dims=re.search(r'size (\d+) (\d+)',c.cmd('setterm'));assert dims
terminal=pyte.Screen(int(dims[1]),int(dims[2]));stream=pyte.Stream(terminal)
c.observe=lambda data:stream.feed(data.decode(errors='replace'))

def save(): a.log.write_text(json.dumps(r,indent=2)+'\n')
def key(name):
    assert 'Queued' in c.cmd('lcd key '+name);time.sleep(.3)
def screen(): return c.cmd('lcd dump')
def current():
    out=screen();m=re.search(r'^\s*\* ([^\n]+)',out,re.M)
    return m[1].strip() if m else None

def blocks():
    out=c.cmd('audio status');m=re.search(r'stereo blocks=(\d+) underruns=(\d+)',out)
    assert m and int(m[2])==0,out
    return int(m[1])

def start(options=''):
    global active
    assert 'Queued' in c.cmd('lcd send '+json.dumps('player '+options+' '+root))
    active=True;time.sleep(.5)
    out=screen();assert 'Folder Player' in out,out
    assert '3 tracks' in out,out
    return out

def stop():
    global active
    key('exit');active=False
    out=c.cmd('audio status');assert 'playback=0 capture=0 bytes' in out,out
    assert c.memory()==baseline,(baseline,c.memory())

def usb_keys(raw,seconds=.4):
    c.conn.write(raw);data=bytearray();end=time.monotonic()+seconds
    while time.monotonic()<end:
        chunk=c.conn.read(16384);data.extend(chunk);c.observe(chunk)
    out=ANSI.sub('',data.decode(errors='replace')).replace('\r','')
    assert 'Fault IRQ:' not in out and 'STACK OVERFLOW:' not in out,out
    view='\n'.join(terminal.display)
    r.setdefault('usb',[]).append({'input':repr(raw),'output':out,'screen':view});return view

try:
    assert 'SGTL5000=ready' in c.cmd('audio status')
    assert 'player' in c.cmd('apps')
    c.cmd('mkdir '+root);c.cmd('mkdir '+root+'/empty');c.cmd('mkdir '+root+'/bad')
    with tempfile.TemporaryDirectory(prefix='player-tones-') as directory:
        files={}
        for name,hz in [('01 Alpha.mp3',440),('03 Gamma.MP3',660)]:
            path=Path(directory)/name
            subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-f','lavfi','-i',
                'sine=frequency=%d:duration=6'%hz,'-af','volume=0.1','-ar','44100',
                '-ac','2','-codec:a','libmp3lame','-b:a','128k',str(path)],check=True)
            files[name]=path.read_bytes()
        path=Path(directory)/'02 Beta.wav'
        subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-f','lavfi','-i',
            'sine=frequency=880:duration=1','-af','volume=0.1','-ar','22050','-ac','1',str(path)],check=True)
        files[path.name]=path.read_bytes()
        files['ignore.txt']=b'not audio';files['.hidden.mp3']=b'not audio'
        files['bad/broken.mp3']=b'not an MP3'
        # Copy fixtures over local Ethernet: character-at-a-time REPL uploads
        # take minutes and are unrelated to the playback test.
        class Handler(BaseHTTPRequestHandler):
            def log_message(self,*args): pass
            def do_GET(self):
                data=files.get(unquote(self.path.lstrip('/')))
                if data is None:self.send_error(404);return
                self.send_response(200);self.send_header('Content-Length',str(len(data)))
                self.send_header('Content-Type','application/octet-stream');self.end_headers()
                self.wfile.write(data)
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as route:
            route.connect(('192.168.1.197',9));host=route.getsockname()[0]
        server=ThreadingHTTPServer((host,0),Handler)
        threading.Thread(target=server.serve_forever,daemon=True).start()
        assert 'up' in c.cmd('network status')
        for name,data in files.items():
            url='http://%s:%d/%s'%(host,server.server_port,quote(name))
            out=c.cmd('curl -o '+json.dumps(root+'/'+name)+' '+url)
            assert 'HTTP 200' in out and 'wrote %d bytes'%len(data) in out,out
        c.exchange(b'python\r',suffix='>>> ')
        c.exchange(b'import binascii, hashlib\r',suffix='>>> ')
        for name,data in files.items():
            out=c.exchange(("f=open(%r,'rb');print(binascii.hexlify(hashlib.sha256(f.read()).digest()).decode());f.close()\r"%(root+'/'+name)).encode(),suffix='>>> ')
            assert hashlib.sha256(data).hexdigest() in out,out
        c.exchange(b'\x04')
    baseline=c.memory();r['memory_before']=baseline;print('Fixtures uploaded',flush=True)
    start();assert current()=='01 Alpha.mp3'
    key('space');b=blocks();time.sleep(1.4);assert blocks()==b
    assert '||' in screen()
    key('space');time.sleep(.3);assert blocks()>b
    key('right');assert current()=='02 Beta.wav';blocks()
    key('left');assert current()=='01 Alpha.mp3';blocks()
    assert 'Audio is in use' in c.cmd('aplay '+json.dumps(root+'/01 Alpha.mp3'))
    assert 'Audio busy' in c.cmd('audio off')
    # Retained UI keeps playing and retains audio ownership.
    key('ctrlz');b=blocks();time.sleep(.4);assert blocks()>b
    assert 'Audio is in use' in c.cmd('synth')
    c.cmd('lcd send fg');time.sleep(.5);assert blocks()>b
    stop();r['pause_transport_ownership_suspend']=True;save();print('Pause, next/previous, ownership and suspend passed',flush=True)
    # Normal ordering stops at EOF; shuffle visits each file exactly once.
    for options in ('','--shuffle'):
        start(options);seen=[];end=time.monotonic()+20
        while time.monotonic()<end:
            name=current()
            if name and (not seen or name!=seen[-1]):seen.append(name)
            status=c.cmd('audio status');assert 'underruns=0' in status,status
            if 'playback=0 capture=0 bytes' in status and len(seen)==3:break
            time.sleep(.2)
        assert len(seen)==3 and len(set(seen))==3,seen
        if not options:assert seen==['01 Alpha.mp3','02 Beta.wav','03 Gamma.MP3'],seen
        assert 'playback=0 capture=0 bytes' in c.cmd('audio status')
        r['shuffle_order' if options else 'normal_order']=seen;stop();save()
        print('Folder cycle passed: '+str(seen),flush=True)
    start('--repeat one');previous=blocks();restarted=False;end=time.monotonic()+16
    while time.monotonic()<end:
        time.sleep(.2);b=blocks()
        restarted |= b<previous
        if restarted and b>0:break
        previous=b
    assert restarted and current()=='01 Alpha.mp3' and b>0;stop()
    start('--repeat all');seen_other=False;end=time.monotonic()+23
    while time.monotonic()<end:
        name=current();seen_other |= name=='03 Gamma.MP3'
        if seen_other and name=='01 Alpha.mp3':break
        time.sleep(.2)
    assert seen_other and current()=='01 Alpha.mp3'
    time.sleep(.7);assert blocks()>0;stop()
    r['repeat_modes']=True;save();print('Repeat modes passed',flush=True)
    # Direct USB keys exercise toggles without LCD command injection adding Enter.
    usb_active=True
    out=usb_keys(('player '+root+'\r').encode(),.6);assert 'Folder Player' in out,out
    volume=int(re.search(r'Vol:(\d+)',out)[1])
    assert 'Shuffle:on' in usb_keys(b's')
    assert 'Repeat:all' in usb_keys(b'r')
    assert 'Vol:%d'%min(volume+5,100) in usb_keys(b'+')
    usb_keys(b' ');usb_keys(b'n');usb_keys(b'p')
    c.exchange(b'q');usb_active=False
    assert c.memory()==baseline,(baseline,c.memory())
    # Same-console workflow requested by the user: leave player running, edit
    # and save text, return to player controls, then recover the editor contents.
    usb_active=True
    usb_keys(('player --repeat all '+root+'\r').encode(),.5)
    out=c.exchange(b'\x1a');usb_active=False
    player_id=re.search(r'Suspended session (\d+)',out)[1]
    before=blocks()
    usb_active=True
    usb_keys(('edit '+root+'/editing.txt\r').encode(),.5)
    text='MP3_PLAYING_WHILE_EDITING'
    usb_keys(text.encode(),8)
    usb_keys(b'\x13',.7) # Save to SD while MP3 decoding continues.
    out=c.exchange(b'\x1a');usb_active=False
    editor_id=re.search(r'Suspended session (\d+)',out)[1]
    assert editor_id!=player_id
    state=c.cmd('sessions');r['editing_sessions']=state
    status=c.cmd('audio status');r['editing_audio']=status
    assert 'running=1' in status and 'underruns=0' in status,status
    assert text in c.cmd('cat '+root+'/editing.txt')
    # Selecting another session keeps the editor in RAM with its own buffer.
    usb_active=True
    usb_keys(('fg '+player_id+'\r').encode(),.5)
    usb_keys(b'n');usb_keys(b' ')
    c.exchange(b'\x1a');usb_active=False
    b=blocks();time.sleep(.4);assert blocks()==b
    usb_active=True
    out=usb_keys(('fg '+editor_id+'\r').encode(),.5)
    assert text in out,out
    usb_keys(b'_STILL_HERE');usb_keys(b'\x13',.4)
    c.exchange(b'\x1d');usb_active=False
    assert '_STILL_HERE' in c.cmd('cat '+root+'/editing.txt')
    usb_active=True
    usb_keys(('fg '+player_id+'\r').encode(),.5);usb_keys(b' ')
    c.exchange(b'\x1a');usb_active=False
    b=blocks();time.sleep(.4);assert blocks()>b
    c.cmd('close '+player_id)
    assert c.memory()==baseline,(baseline,c.memory())
    r['editing_while_playing']=True;save();print('Player/editor session switching, save and resume passed',flush=True)
    # Empty, absent, invalid arguments and corrupt audio release resources.
    for path in (root+'/empty',root+'/missing'):
        out=c.cmd('player '+path);assert 'failed' in out,out
        assert c.memory()==baseline
    out=c.cmd('player --repeat bad '+root);assert 'failed' in out or 'usage' in out,out
    c.cmd('lcd send '+json.dumps('player '+root+'/bad'));active=True
    end=time.monotonic()+5
    while time.monotonic()<end:
        time.sleep(.2);out=screen()
        if 'Playback failed' in out:break
    assert 'Playback failed' in out,out
    stop()
    for _ in range(3):start('--shuffle');stop()
    r['memory_after']=c.memory();assert r['memory_after']==baseline
    r['passed']=True;print('PASS: folder player, controls, repeat/shuffle, failures and memory recovery',flush=True)
finally:
    if usb_active:
        c.exchange(b'\x03')
    if active:
        c.cmd('lcd key exit');time.sleep(.4)
    # Clean only player/editor sessions created by this test, including a
    # retained player if an assertion failed while the shell/editor was active.
    for sid,app in re.findall(r'^(\d+)\s+\S+\s+(?:active|suspended)\s+(\S+)',c.cmd('sessions'),re.M):
        if sid not in initial_ids and app in ('player','edit'):c.cmd('close '+sid)
    time.sleep(.3)
    c.cmd('rm -r '+root)
    assert 'No such file or directory' in c.cmd('ls '+root)
    r['fixtures_removed']=True
    c.close()
    if server:server.shutdown();server.server_close()
    save()
