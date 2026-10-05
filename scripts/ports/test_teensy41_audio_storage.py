#!/usr/bin/env python3
"""Characterize SD/flash microphone write stalls; retain only host JSON results.
Uses unique files, never replaces user data. A failed recording is reported,
not confused with a passing real-time capture.
"""
import argparse,json,re,time,uuid
from pathlib import Path
from teensy41_audio_probe import Console
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--log',type=Path,required=True)
a=p.parse_args();r={'passed':False,'recordings':[]};c=Console(r)
try:
    assert 'SGTL5000=ready' in c.cmd('audio status')
    baseline=c.memory();r['memory_before']=baseline
    for storage in ('sd','flash'):
        for seconds in (4,10,30):
            path='/'+storage+'/_audio_storage_'+uuid.uuid4().hex[:8]+'.wav'
            start=time.monotonic();out=c.cmd('arecord -d %d %s'%(seconds,path),timeout=seconds+30)
            elapsed=time.monotonic()-start;status=c.cmd('audio status')
            drops=int(re.search(r'overruns=(\d+)',status)[1])
            record={'storage':storage,'seconds':seconds,'elapsed':elapsed,'result':out,'status':status,'overruns':drops,
                    'wav_completed':'arecord: done' in out,
                    'passed':'arecord: done' in out and drops==0 and elapsed<seconds+.75}
            r['recordings'].append(record)
            assert 'playback=0 capture=0 bytes' in status,status
            c.cmd('rm '+path);assert 'No such file or directory' in c.cmd('ls '+path)
            assert c.memory()==baseline,(baseline,c.memory())
            a.log.write_text(json.dumps(r,indent=2)+'\n')
            print(json.dumps(record),flush=True)
    r['memory_after']=c.memory();r['passed']=all(x['passed'] for x in r['recordings']);r['fixtures_removed']=True
finally:
    c.close();a.log.write_text(json.dumps(r,indent=2)+'\n')
