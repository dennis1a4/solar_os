#!/usr/bin/env python3
"""Check dashboard pages on an idle Teensy over USB; no NES/radio/audio required."""
import argparse
import json
import re
import time
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_ftp import Console


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log',type=Path,required=True)
    args=parser.parse_args()
    report={'commands':[],'passed':False}
    ports=[p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x16c0,0x0483)]
    assert len(ports)==1,ports
    with serial.Serial(ports[0],115200,timeout=.03,write_timeout=3,exclusive=True) as usb:
        c=Console(usb,report)
        c.command('')
        sessions=c.command('sessions')
        assert not re.search(r'attached\s+(?!shell\b)\w+',sessions),sessions
        try:
            for page,marker in [('system','DTCM'),('network','Network'),('music','Player is not active'),('mesh','Mesh slot0')]*2:
                c.command('lcd small page '+page)
                out=c.poll('lcd small',lambda text:marker in text,timeout=40)
                assert '< > Page' in out,out
                lines=out.splitlines()
                start=next(i for i,line in enumerate(lines) if line.startswith('small='))+1
                assert all(len(line)<=26 for line in lines[start:start+9]),out
                assert int(re.search(r'stack-free=(\d+)',out)[1])>=512,out
                print('PASS:',page,flush=True)
            c.command('lcd small page music')
            c.lcd('player /sd/music')
            out=c.poll('lcd small',lambda text:'Volume ' in text,timeout=15)
            assert 'A Play/Pause' in out,out
            print('PASS: player snapshot available',flush=True)
            c.command('lcd key esc')
            c.poll('sessions',lambda text:'player' not in text)
            c.poll('lcd small',lambda text:'Player is not active' in text)
            print('PASS: player snapshot cleared on exit',flush=True)
            c.command('lcd small page network')
            c.lcd('gameboy /sd/roms/2048.gb')
            c.poll('sessions',lambda text:'gameboy' in text)
            c.command('lcd small page mesh')
            time.sleep(2)
            assert 'gameboy' in c.command('sessions')
            c.command('lcd key esc')
            c.poll('sessions',lambda text:'gameboy' not in text)
            c.poll('lcd small',lambda text:'Mesh slot0' in text)
            print('PASS: Game Boy ownership and dashboard restore',flush=True)
            report['passed']=True
        finally:
            c.command('lcd key esc')
            c.command('lcd small page system')
            report['final']=c.poll('lcd small',lambda text:'DTCM' in text,timeout=40)
            report['memory']=c.command('mem')
            args.log.write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':main()
