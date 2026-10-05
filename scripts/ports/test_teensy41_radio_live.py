#!/usr/bin/env python3
"""WebRadio + four-second microphone monitor. No saved station changes.
For local deterministic input, serve --fixture MP3 at 128 kb/s; alternatively
--url selects a live station. Uses unique RAMFS capture; removes it on success.
"""
import argparse,io,json,re,socket,threading,time,uuid,wave
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
import numpy as np
from teensy41_audio_probe import Console
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--fixture',type=Path)
p.add_argument('--url')
p.add_argument('--log',type=Path,required=True)
p.add_argument('--wav',type=Path,required=True)
a=p.parse_args();assert bool(a.fixture)!=bool(a.url)
r={'passed':False};server=None;c=None;active=False;mount='/ar'+uuid.uuid4().hex[:6]
try:
    url=a.url
    if a.fixture:
        data=a.fixture.read_bytes()
        class Handler(BaseHTTPRequestHandler):
            def log_message(self,*args):pass
            def do_GET(self):
                self.send_response(200);self.send_header('Content-Type','audio/mpeg');self.send_header('Content-Length',str(len(data)));self.end_headers()
                started=time.monotonic()
                try:
                    for offset in range(0,len(data),1024):
                        self.wfile.write(data[offset:offset+1024]);self.wfile.flush()
                        pause=started+(offset+1024)/16000-time.monotonic()
                        if pause>0:time.sleep(pause)
                except (BrokenPipeError,ConnectionResetError):pass
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as route:
            route.connect(('192.168.1.197',9));host=route.getsockname()[0]
        server=ThreadingHTTPServer((host,0),Handler)
        threading.Thread(target=server.serve_forever,daemon=True).start()
        url='http://%s:%d/test.mp3'%(host,server.server_port)
    r['url']=url;c=Console(r)
    assert 'SGTL5000=ready' in c.cmd('audio status')
    assert 'up' in c.cmd('network status')
    c.cmd('webradio list') # warm catalog allocation before memory baseline
    baseline=c.memory();r['memory_before']=baseline
    out=c.cmd('ramfs mount '+mount+' 512k');assert 'FAIL' not in out,out
    out=c.cmd('lcd send '+json.dumps('webradio '+url));assert 'Queued' in out,out;active=True
    deadline=time.monotonic()+30
    while time.monotonic()<deadline:
        time.sleep(1)
        status=c.cmd('audio status')
        m=re.search(r'stereo blocks=(\d+) underruns=(\d+)',status)
        if m and int(m[1])>100:break
    else:raise AssertionError('No radio audio: '+c.cmd('lcd dump'))
    r['initial_status']=status;r['screen']=c.cmd('lcd dump');r['tasks']=c.cmd('top')
    # Confirm tone/off guard cannot disrupt the live app.
    assert 'Audio busy' in c.cmd('audio off')
    time.sleep(2)
    before=c.cmd('audio status')
    out=c.cmd('audio monitor '+mount+'/radio.wav')
    r['capture_result']=out
    assert 'Audio monitor: OK, 352800 bytes, 4000 ms' in out,out
    assert 'INVALID_STATE' in c.cmd('audio monitor '+mount+'/radio.wav')
    status=c.cmd('audio status');r['capture_status']=status
    assert 'overruns=0' in status,status
    assert 'playback=16384 capture=0 bytes' in status,status
    # Stop after a short listening interval, then transfer the acoustic sample.
    time.sleep(5);r['final_status']=c.cmd('audio status');r['final_screen']=c.cmd('lcd dump')
    c.cmd('lcd key exit');active=False;time.sleep(.3)
    data=c.download(mount+'/radio.wav');a.wav.write_bytes(data)
    with wave.open(io.BytesIO(data),'rb') as f:
        assert (f.getnchannels(),f.getsampwidth(),f.getframerate(),f.getnframes())==(1,2,44100,176400)
        samples=np.frombuffer(f.readframes(f.getnframes()),dtype='<i2').astype(float)
    r['clipped_samples']=int(np.count_nonzero(np.abs(samples)>=32760))
    r['peak_to_peak']=float(np.ptp(samples))
    # 100 ms Hann windows separate 440 Hz from the 420 Hz hum harmonic.
    # Windowed tone energy detects sustained dropouts in the controlled stream; music
    # silence cannot be classified as a dropout from amplitude alone.
    energies=[]
    for start in range(4410,len(samples)-4410,441):
        x=samples[start:start+4410];x=x-x.mean();window=np.hanning(len(x))
        z=abs(np.sum(x*window*np.exp(-2j*np.pi*440*np.arange(len(x))/44100)))
        energies.append(float(z))
    noise_powers=[]
    for frequency in (370,390,490,510):
        values=[]
        for start in range(4410,len(samples)-4410,441):
            x=samples[start:start+4410];x=x-x.mean()
            z=abs(np.sum(x*np.hanning(len(x))*np.exp(-2j*np.pi*frequency*np.arange(len(x))/44100)))
            values.append(float(z)**2)
        noise_powers.append(float(np.median(values)))
    r['tone_snr_db']=float(10*np.log10((float(np.median(np.square(energies)))+1)/(float(np.median(noise_powers))+1)))
    r['tone_detected']=r['tone_snr_db']>10
    r['tone_100ms_min_median_ratio']=min(energies)/max(float(np.median(energies)),1)
    r['tone_100ms_weak_windows']=sum(x<float(np.median(energies))*.2 for x in energies)
    c.cmd('rm '+mount+'/radio.wav');c.cmd('ramfs unmount '+mount)
    r['memory_after']=c.memory();assert r['memory_after']==baseline,(baseline,r['memory_after'])
    assert 'playback=0 capture=0 bytes' in c.cmd('audio status')
    r['fixture_removed']=True
    statuses=[r['initial_status'],r['capture_status'],r['final_status']]
    r['playback_underruns']=[int(re.search(r'underruns=(\d+)',s)[1]) for s in statuses]
    r['transport_passed']=all(n==0 for n in r['playback_underruns'])
    r['acoustic_continuity_passed']=(r['tone_100ms_weak_windows']==0) if a.fixture and r['tone_detected'] else None
    r['passed']=r['transport_passed']
    # The recording is evidence, not automatic proof that arbitrary music has
    # no stutter. Controlled tone continuity also needs an audible tone signal.

    print(json.dumps({k:v for k,v in r.items() if k!='commands'},indent=2),flush=True)
finally:
    if c:
        if active:
            try:c.cmd('lcd key exit')
            except Exception:pass
        c.close()
    if server:server.shutdown();server.server_close()
    a.log.write_text(json.dumps(r,indent=2)+'\n')
