#!/usr/bin/env python3
"""Teensy acceptance for mesh service with NO radio attached to slot0/slot1.
Requires idle USB/LCD shells and SD. Verifies resource rejection, both concurrent
handshake timeouts, app notifications, logging, and release without transmitting RF.
Writes a unique SD TSV retained for inspection. Does not flash or reboot.
"""
import argparse
import json
import re
import time
import uuid
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_ftp import Console
from test_teensy41_hotplug import require


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log',type=Path,required=True)
    args=parser.parse_args()
    report={'passed':False,'commands':[],'checks':[]}
    path='/sd/_mesh_'+uuid.uuid4().hex[:8]+'.tsv'
    report['fixture']=path
    def passed(text):
        report['checks'].append(text);print('PASS:',text,flush=True)
        args.log.write_text(json.dumps(report,indent=2)+'\n')
    ports=[p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x16c0,0x0483)]
    require(len(ports)==1,ports)
    with serial.Serial(ports[0],115200,timeout=.03,write_timeout=3,exclusive=True) as usb:
        c=Console(usb,report)
        original_command=c.command
        def command(text,python=False):
            try:
                result=original_command(text,python=python)
                print('checked:',text,flush=True)
                return result
            finally:
                args.log.write_text(json.dumps(report,indent=2)+'\n')
        c.command=command
        c.command('')
        require('mesh:slot' not in c.command('io claims'),'Mesh service already in use')
        try:
            require('meshcore, meshtastic' in c.command('mesh drivers'),'drivers')
            require('slot0: disconnected' in c.command('mesh'),'default status')
            require('chat' in c.command('apps') and 'meshmon' in report['commands'][-1]['output'],'app registry')
            require('command.mesh' in c.command('man -k mesh'),'manual')
            require('mesh: OK' not in c.command('mesh connect slot2 meshcore'),'display pins must conflict')
            require('mesh: OK' not in c.command('mesh connect slot0 unknown'),'unknown driver')
            before=c.command('mem');report['cold_memory']=before
            require('mesh: OK' in c.command('mesh connect slot0 meshcore'),'MeshCore start')
            require('mesh: OK' not in c.command('mesh connect slot0 meshtastic'),'exclusive UART')
            require('mesh: OK' in c.command('mesh connect slot1 meshtastic'),'Meshtastic start')
            claims=c.command('io claims');require('mesh:slot0' in claims and 'mesh:slot1' in claims,claims)
            passed('both runtime drivers, separate slots and resource protection')
            require('radio not connected' in c.command('mesh send slot0 0 test'),'offline send rejected')
            require('two ready radios' in c.command('meshbridge on slot0 0 slot1 0'),'offline bridge rejected')
            c.command('meshlog start slot0 '+path)
            require('log ON' in c.command('meshlog status slot0'),'log start')
            c.lcd('chat slot0')
            c.poll('lcd dump',lambda out:'Mesh chat' in out)
            # A second consumer on USB must not consume the chat/logger event cursor.
            usb.write(b'meshmon slot1\r');raw=b'';deadline=time.monotonic()+1
            while time.monotonic()<deadline:raw+=usb.read(16384)
            require(b'Mesh monitor' in raw,raw.decode(errors='replace'))
            usb.write(b'\x1b');time.sleep(.15);c.command('')
            passed('independent LCD chat and USB monitor')
            out=c.poll('meshstatus',lambda out:out.count('Radio response timeout')==2,timeout=15)
            require(out.count('timeouts: 1')==2,out)
            c.poll('lcd dump',lambda out:'failed' in out or 'timeout' in out)
            c.command('meshlog stop slot0')
            log=c.command('cat '+path);require('Radio response timeout' in log,log)
            require('WRITE FAILED' not in c.command('meshlog status slot0'),'log write failed')
            passed('background timeouts notify app and SD logger without a radio')
            c.command('mesh disconnect slot0');c.command('mesh disconnect slot1')
            c.poll('lcd dump',lambda out:'disconnected' in out)
            c.command('lcd key esc');c.poll('sessions',lambda out:'chat' not in out)
            require('mesh:slot' not in c.command('io claims'),'resource leak')
            warm=c.command('mem');report['warm_memory']=warm
            def regions(text):
                return {name:int(value) for name,value in re.findall(r'(DTCM|OCRAM|PSRAM) \([^)]*\): used=(\d+)',text)}
            before=regions(warm)
            for driver in ('meshcore','meshtastic','meshcore','meshtastic'):
                require('mesh: OK' in c.command('mesh connect slot0 '+driver),'reconnect failed')
                c.command('mesh disconnect slot0')
            after=regions(c.command('mem'));report['after_cycles']=after
            require(after==before,(before,after))
            passed('disconnect releases UART/PSRAM; repeated driver switching has no measured heap growth')
            report['passed']=True
        finally:
            usb.write(b'\x1b');time.sleep(.15)
            c.command('meshbridge off');c.command('meshlog stop slot0')
            c.command('mesh disconnect slot0');c.command('mesh disconnect slot1')
            c.command('lcd key esc')
            args.log.write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':main()
