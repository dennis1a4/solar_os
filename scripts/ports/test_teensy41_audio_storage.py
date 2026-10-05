#!/usr/bin/env python3
"""Verify sustained SD microphone recording, WAV finalization and memory recovery.
Uses unique files and removes them; saves only host JSON, not ambient audio.
Flash characterization remains opt-in and is not long-recording acceptance.
"""
import argparse, json, re, struct, time, uuid
from pathlib import Path
from teensy41_audio_probe import Console

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--log', type=Path, required=True)
p.add_argument('--storage', choices=['sd', 'flash'], default='sd')
p.add_argument('--seconds', type=int, nargs='+', default=[4, 30, 180])
a = p.parse_args()
assert all(0 < s <= 3600 for s in a.seconds)
r = {'passed': False, 'recordings': []}
c = Console(r)
paths = []

def new_path():
    path = '/' + a.storage + '/_audio_storage_' + uuid.uuid4().hex[:8] + '.wav'
    paths.append(path)
    return path

def wav_header(path):
    c.exchange(b'python\r', suffix='>>> ')
    source = ("import binascii;f=open(%r,'rb');h=f.read(44);f.seek(0,2);"
              "print('WAV:'+binascii.hexlify(h).decode()+':'+str(f.tell()));f.close()" % path)
    out = c.exchange((source + '\r').encode(), suffix='>>> ')
    c.exchange(b'\x04')
    m = re.search(r'^WAV:([0-9a-f]+):(\d+)$', out, re.M)
    assert m, out
    h, size = bytes.fromhex(m[1]), int(m[2])
    assert h[:4] == b'RIFF' and h[8:16] == b'WAVEfmt ' and h[36:40] == b'data'
    assert struct.unpack_from('<I', h, 4)[0] == size - 8
    assert struct.unpack_from('<HHIIHH', h, 20) == (1, 1, 44100, 88200, 2, 16)
    data = struct.unpack_from('<I', h, 40)[0]
    assert data == size - 44 and data % 2 == 0
    return data

def check_idle(baseline):
    status = c.cmd('audio status')
    assert 'playback=0 capture=0 bytes' in status, status
    assert 'allocated=0 ' in status, status
    headroom = int(re.search(r'feeder stack free=(\d+)', status)[1])
    assert headroom == 0 or headroom >= 512, status
    assert c.memory() == baseline, (baseline, c.memory())
    return status

try:
    assert 'SGTL5000=ready' in c.cmd('audio status')
    # Warm the Python VM once before comparing exact idle heap amounts.
    c.exchange(b'python\r', suffix='>>> '); c.exchange(b'\x04')
    baseline = c.memory(); r['memory_before'] = baseline
    for seconds in a.seconds:
        path = new_path()
        print('Recording %d seconds to %s' % (seconds, path), flush=True)
        start = time.monotonic()
        out = c.cmd('arecord -d %d %s' % (seconds, path), timeout=seconds + 30)
        elapsed = time.monotonic() - start
        status = check_idle(baseline)
        drops = int(re.search(r'overruns=(\d+)', status)[1])
        data = wav_header(path)
        record = {'storage': a.storage, 'seconds': seconds, 'elapsed': elapsed,
                  'result': out, 'status': status, 'overruns': drops, 'data_bytes': data,
                  'passed': 'arecord: done' in out and drops == 0 and
                            data == seconds * 88200 and abs(elapsed - seconds) < .75}
        r['recordings'].append(record)
        a.log.write_text(json.dumps(r, indent=2) + '\n')
        print(json.dumps(record), flush=True)
        assert record['passed'], record
        assert 'INVALID_STATE' in c.cmd('arecord -d 1 ' + path)
        assert wav_header(path) == data
        c.cmd('rm ' + path); paths.remove(path)
        check_idle(baseline)
    # Stop an unlimited recording while both capture stages are active.
    path = new_path()
    c.conn.write(('arecord ' + path + '\r').encode()); time.sleep(2)
    start = time.monotonic(); out = c.exchange(b'\x03')
    r['cancel_latency'] = time.monotonic() - start
    assert 'arecord: stopped' in out and r['cancel_latency'] < 2, out
    data = wav_header(path)
    assert 88200 < data < 3 * 88200, data
    r['cancel_data_bytes'] = data
    assert 'overruns=0' in check_idle(baseline)
    c.cmd('rm ' + path); paths.remove(path)
    # A bad destination must leave neither capture resources nor an open file.
    out = c.cmd('arecord -d 1 /sd/_missing_' + uuid.uuid4().hex[:8] + '/audio.wav')
    assert 'arecord: done' not in out
    check_idle(baseline)
    r['memory_after'] = c.memory(); r['passed'] = True
finally:
    for path in paths:
        c.cmd('rm ' + path)
    r['fixtures_removed'] = True
    c.close(); a.log.write_text(json.dumps(r, indent=2) + '\n')
