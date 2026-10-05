#!/usr/bin/env python3
"""Record the nearby speaker's test tone, retrieve WAV and measure 440 Hz energy.
Uses unique SD/flash recordings; removes device fixtures on success.
Host WAV/log may contain ambient audio.
"""
import argparse, base64, io, json, re, time, uuid, wave
from pathlib import Path
import numpy as np
import serial
from serial.tools import list_ports
from test_teensy41_shell import ANSI, PROMPT

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--log', type=Path, required=True)
p.add_argument('--wav', type=Path, required=True)
p.add_argument('--storage', choices=['sd','flash'], default='sd')
p.add_argument('--ram', action='store_true', help='Capture in temporary RAMFS to isolate storage stalls')
a=p.parse_args()
ram_mount='/am'+uuid.uuid4().hex[:6] if a.ram else None
root=(ram_mount if ram_mount else '/'+a.storage)+'/_solaros_mic_'+uuid.uuid4().hex[:10]
r=dict(path=root+'.wav', passed=False, commands=[])
try:
    ports=[p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x16c0,0x0483)]
    assert len(ports)==1, ports
    with serial.Serial(ports[0],115200,timeout=.02,write_timeout=3,exclusive=True) as conn:
        time.sleep(1); conn.reset_input_buffer()
        def exchange(raw, suffix=PROMPT, expected=None, record=True):
            if raw: conn.write(raw)
            data=bytearray(); deadline=time.monotonic()+20
            while time.monotonic()<deadline:
                data.extend(conn.read(8192))
                text=ANSI.sub('',data.decode(errors='replace')).replace('\r','')
                if text.endswith(suffix):
                    if record: r['commands'].append(dict(input=repr(raw), output=text))
                    if expected is not None: assert expected in text, text[-1500:]
                    return text
            raise RuntimeError(f'Timeout {raw!r}: {data[-1000:]!r}')
        def cmd(s, expected=None): return exchange((s+'\r').encode(),expected=expected)
        def py(s, record=True): return exchange((s+'\r').encode(),suffix='>>> ',record=record)
        cmd(''); cmd('audio status','SGTL5000=ready')
        if ram_mount:cmd('ramfs mount '+ram_mount+' 1m')
        tone_result = cmd('audio mictest '+root+'.wav','Mic test: OK, 352800 bytes, 4000 ms')
        tone_count = re.search(r'Mic test tone: started=1 blocks=(\d+)', tone_result)
        assert tone_count and 340 <= int(tone_count[1]) <= 350, tone_result
        cmd('audio status','overruns=0')
        # Cancellation must finalize a valid partial recording and return promptly.
        conn.write(('arecord '+root+'_cancel.wav\r').encode()); time.sleep(.3)
        exchange(b'\x03',expected='arecord: stopped')
        cmd('arecord -d 1 '+root+'.wav','INVALID_STATE')
        cmd('arecord -d 1 '+root+'_short.wav','arecord: done')
        exchange(b'python\r',suffix='>>> ')
        py('import binascii, struct')
        partial = py("f=open('"+root+"_cancel.wav','rb'); h=f.read(44); f.seek(0,2); assert struct.unpack('<I',h[40:44])[0]==f.tell()-44; f.close(); print('partial WAV OK')")
        assert "\npartial WAV OK\n" in partial, partial
        py("f=open('"+root+".wav','rb')")
        result=bytearray()
        while True:
            text=py("print('DATA:'+binascii.b2a_base64(f.read(768)).decode().strip())",record=False)
            line=re.search(r'^DATA:([A-Za-z0-9+/=]*)$',text,re.M)
            assert line, text[-1000:]
            block=base64.b64decode(line[1],validate=True)
            if not block: break
            result.extend(block)
            assert len(result)<=352844
        py('f.close()'); exchange(b'\x04')
        assert 'playback=0 capture=0 bytes' in cmd('audio status')
        for path in [root+'.wav',root+'_cancel.wav',root+'_short.wav']:
            cmd('rm '+path)
            assert 'No such file or directory' in cmd('ls '+path)
        if ram_mount:cmd('ramfs unmount '+ram_mount)
        r['fixtures_removed']=True
        r['status']=cmd('audio status'); r['memory']=cmd('mem'); r['uptime']=cmd('uptime')
    a.wav.write_bytes(result)
    with wave.open(io.BytesIO(result),'rb') as f:
        assert (f.getnchannels(),f.getsampwidth(),f.getframerate(),f.getnframes())==(1,2,44100,176400)
        samples=np.frombuffer(f.readframes(f.getnframes()),dtype='<i2').astype(float)
    def metrics(start,end):
        x=samples[int(start*44100):int(end*44100)]; x=x-x.mean()
        spectrum=np.abs(np.fft.rfft(x*np.hanning(len(x))))
        frequencies=np.fft.rfftfreq(len(x),1/44100)
        return dict(rms=float(np.sqrt(np.mean(x*x))), peak=float(np.max(np.abs(x))),
                    tone_band=float(np.sum(spectrum[(frequencies>=430)&(frequencies<=450)]**2)),
                    peak_hz=float(frequencies[np.argmax(spectrum)]))
    r['before']=metrics(.2,.8); r['tone']=metrics(1.2,1.8); r['after']=metrics(2.5,3.1)
    r['tone_gain_db']=float(10*np.log10((r['tone']['tone_band']+1)/(r['before']['tone_band']+1)))
    r['tone_detected']=r['tone_gain_db']>10 and r['tone']['tone_band']>10*(r['after']['tone_band']+1)
    r['clipped_samples']=int(np.count_nonzero(np.abs(samples)>=32760))
    assert np.ptp(samples)>0, 'Input is constant; check pin 8 and microphone wiring'
    assert r['tone_detected'], 'No clear rise/fall of the 440 Hz speaker tone in the recording'
    r['passed']=True
    print(json.dumps({k:r[k] for k in ['path','before','tone','after','tone_gain_db','clipped_samples']},indent=2))
    print('PASS: recording size/header, cancellation, overwrite protection and capture data; inspect acoustic metrics')
except Exception as exc:
    r['error']=str(exc)
    raise
finally:
    a.log.write_text(json.dumps(r,indent=2)+'\n')
