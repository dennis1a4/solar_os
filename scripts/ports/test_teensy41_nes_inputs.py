#!/usr/bin/env python3
"""Read-only dual-reader acceptance over USB; physical presses still need a person."""
import re
import time
import serial
from serial.tools import list_ports
from test_teensy41_ftp import Console

ports = [p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x16c0,0x0483)]
assert len(ports)==1,ports
with serial.Serial(ports[0],115200,timeout=.03,write_timeout=3,exclusive=True) as usb:
    c=Console(usb,{'commands':[]})
    c.command('')
    start=None
    for _ in range(6):
        out=c.command('lcd small')
        print(out.split('small=',1)[1].splitlines()[0],flush=True)
        assert 'nes=flexio-dual-dma' in out and 'pads=2' in out,out
        assert 'nes-faults=0' in out,out
        assert re.search(r'onboard=[0-9a-f]{2} external=[0-9a-f]{2}',out),out
        scans=int(re.search(r'nes-scans=(\d+)',out)[1])
        if start is None:start=scans
        time.sleep(1)
    assert scans-start>=100,(start,scans)
    print('PASS: live paired FlexIO DMA scanning, two masks, no reader faults; physical button/timing checks remain pending',flush=True)
