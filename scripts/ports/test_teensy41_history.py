#!/usr/bin/env python3
"""Exclusive USB history acceptance: flash autosave, LCD isolation, reboot recall.
Requires both consoles idle. Reboots the board once; adds harmless echo commands.
"""
import argparse,json,re,time,uuid
from pathlib import Path
import serial
from serial.tools import list_ports
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--log',type=Path,required=True);args=p.parse_args()
report={'passed':False,'commands':[]};conn=None
marker='history_'+uuid.uuid4().hex[:8]
def connect():
    deadline=time.monotonic()+25
    while time.monotonic()<deadline:
        ports=[p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x16c0,0x0483)]
        if len(ports)==1:
            try:
                c=serial.Serial(ports[0],115200,timeout=.03,write_timeout=3,exclusive=True);time.sleep(.7);c.reset_input_buffer();return c
            except serial.SerialException:pass
        time.sleep(.2)
    raise RuntimeError('Teensy did not return')
def exchange(raw):
    conn.write(raw);data=b'';deadline=time.monotonic()+15
    while time.monotonic()<deadline:
        data+=conn.read(16384)
        text=re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]','',data.decode(errors='replace')).replace('\r','')
        if re.search(r'[\w.-]+@[\w.-]+:/[^\n]* $',text):
            report['commands'].append({'input':repr(raw),'output':text});return text
    raise RuntimeError(repr(data[-1000:]))
def cmd(s):return exchange((s+'\r').encode())
try:
    conn=connect();cmd('');cmd('echo '+marker+'_usb')
    cmd('lcd key ctrlc')
    cmd('lcd send "echo '+marker+'_lcd"');cmd('lcd key enter')
    # Give both owners one batching interval without adding further commands.
    time.sleep(31)
    usb=cmd('cat /flash/.shell/history-usb');lcd=cmd('cat /flash/.shell/history-lcd')
    assert 'echo '+marker+'_usb' in usb,usb
    assert '\necho '+marker+'_lcd\n' in lcd and marker+'_usb' not in lcd,lcd
    cmd('echo '+marker+'_reboot')
    conn.write(b'reboot\r');time.sleep(.5);conn.close();conn=None
    time.sleep(2);conn=connect();cmd('')
    out=exchange(b'\x1b[A\x1b[A\r')
    assert re.search(r'^'+marker+'_reboot$',out,re.M),out
    cmd('lcd key up');cmd('lcd key enter');out=cmd('lcd dump');assert marker+'_lcd' in out,out
    cmd('poweroff --check');time.sleep(1)
    out=cmd('poweroff status');assert 'check complete' in out,out
    cmd('mem');report['passed']=True
    print('PASS: timed flash save, separate LCD/USB histories, Up recall across reboot, shutdown flush')
finally:
    if conn:conn.close()
    args.log.write_text(json.dumps(report,indent=2)+'\n')
