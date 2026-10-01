#!/usr/bin/env python3
"""Stage 4 acceptance; exclusive USB, idle keyboard, unchanged board wiring.
No external SPI/I2C data writes. Uses input mode on spare pin 28 and opens UART7/8
(8N1 TX idle high); optional --loopback uart7|uart8 requires an RX/TX jumper.
"""
import argparse,json,re,time
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_hotplug import ANSI
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--log',type=Path,required=True);p.add_argument('--loopback',choices=('uart7','uart8'));a=p.parse_args()
r={'passed':False,'commands':[],'loopback':a.loopback};conn=None;sid=None

def save():a.log.write_text(json.dumps(r,indent=2)+'\n')
def connect():
    ports=[p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x16c0,0x0483)];assert len(ports)==1,ports
    return serial.Serial(ports[0],115200,timeout=.03,write_timeout=3,exclusive=True)
def exchange(raw=b'',prompt=True,seconds=15):
    conn.write(raw);data=bytearray();start=last=time.monotonic();out='';ready=False
    while time.monotonic()-start<seconds:
        chunk=conn.read(16384)
        if chunk:data.extend(chunk);last=time.monotonic()
        out=ANSI.sub('',data.decode(errors='replace')).replace('\r','')
        ready=bool(re.search(r'[\w.-]+@[\w.-]+:/[^\n]* (?:$|\[\d+\])',out))
        if prompt and ready and time.monotonic()-last>.15:break
    r['commands'].append({'input':repr(raw),'output':out});save()
    assert 'Fault IRQ:' not in out,out
    if prompt:assert ready,out[-1800:]
    return out
def cmd(s):return exchange((s+'\r').encode())
def mem():
    m=re.search(r'Internal heap: (\d+) free / \d+ bytes; PSRAM: (\d+) free',cmd('mem'));assert m;return list(map(int,m.groups()))
def snapshot():
    out=cmd('io claims');return sorted(line for line in out.splitlines() if re.match(r'^(gpio|uart|i2c|spi|adc|pwm)',line))
def free(pin):return bool(re.search(r'^\s*'+str(pin)+r' free$',cmd('gpio '+str(pin)),re.M))
try:
    conn=connect();time.sleep(.5);exchange(b'\r');cmd('cd /')
    initial=snapshot();r['baseline_claims']=initial;assert len(initial)>40,initial
    out=cmd('io pins');assert 'primary-display' in out and 'console' in out,out
    assert 'uart7 RX28 TX29' in cmd('io buses')
    for pin in (0,1,7,8,9,13,15,18,37,40):assert 'busy:' in cmd(f'gpio mode {pin} out 0')
    for command in ('gpio mode 99 out 1','gpio write 28 2','uart open uart7 0','spi xfer slot1 0 0 00','i2c xfer i2c1 0x00 - 0'):
        assert 'usage:' in cmd(command),command
    assert 'INVALID_STATE' in cmd('uart open uart3')
    for slot in ('slot0','slot2'):assert 'INVALID_STATE' in cmd('expansion claim '+slot)
    assert snapshot()==initial
    # GPIO/UART atomic exclusion in both directions.
    assert 'gpio: OK' in cmd('gpio mode 28 in')
    assert 'INVALID_STATE' in cmd('uart open uart7')
    assert free(29)
    cmd('gpio release 28');assert 'uart: OK' in cmd('uart open uart7 9600')
    assert 'busy:' in cmd('gpio mode 29 in')
    assert 'open failed' in cmd('com uart7')
    assert 'uart: 0 bytes' in cmd('uart read uart7')
    assert 'uart: OK' in cmd('uart close uart7');assert free(28) and free(29)
    # Slot CS ownership must be visible and reject another console.
    assert 'expansion: OK' in cmd('expansion claim slot1')
    assert 'busy:' in cmd('gpio mode 36 in')
    cmd('lcd send "expansion claim slot1"');time.sleep(.3)
    assert 'expansion: INVALID_STATE' in cmd('lcd dump')
    cmd('expansion release slot1');assert free(36)
    # A reserved I2C address rejects before a transaction can reach the driver.
    assert 'INVALID_STATE' in cmd('i2c xfer i2c0 0x0a - 1')
    assert 'INVALID_STATE' in cmd('spi xfer slot1 0 1000000 00')
    # COM is the shared resumable app, with its lease retained across Ctrl+Z.
    out=exchange(b'com uart7\r',False,.4);assert 'UART7 9600' in out,out
    out=exchange(b'\x1a');m=re.search(r'Suspended session (\d+)',out);assert m,out;sid=int(m[1])
    assert 'app-com' in cmd('uart list')
    assert 'INVALID_STATE' in cmd('uart open uart7')
    cmd('io release');assert 'app-com' in cmd('uart list')
    cmd('lcd send "uart open uart7"');time.sleep(.2);assert 'uart: INVALID_STATE' in cmd('lcd dump')
    exchange(('fg '+str(sid)+'\r').encode(),False,.3);exchange(b'\x1d');sid=None
    assert free(28) and free(29)
    if a.loopback:
        cmd('uart open '+a.loopback)
        assert 'uart: OK' in cmd('uart write '+a.loopback+' Loopback123')
        time.sleep(.1);assert b'Loopback123'.hex() in cmd('uart read '+a.loopback+' 32')
        cmd('uart close '+a.loopback)
        out=exchange(('com '+a.loopback+'\r').encode(),False,.3);assert 'COM '+a.loopback in out,out
        out=exchange(b'UART_RX_TX_OK',False,.4);assert 'UART_RX_TX_OK' in out,out
        exchange(b'\x1d')
    # Every close/disconnect must release pins and hardware, not board claims.
    r['memory']=[]
    for i in range(5):
        exchange(b'com --hex uart8\r',False,.2);exchange(b'\x1d')
        assert 'gpio: OK' in cmd('gpio mode 28 in');cmd('io release')
        assert snapshot()==initial;r['memory'].append(mem())
    assert all(v==r['memory'][0] for v in r['memory']),r['memory']
    cmd('uart open uart7');cmd('expansion claim slot1');cmd('gpio mode 14 in')
    conn.close();time.sleep(.8);conn=connect();time.sleep(.3);exchange(b'\r')
    assert snapshot()==initial
    # The interactive app releases its own separate lease on disconnect too.
    exchange(b'com uart8\r',False,.2);conn.close();time.sleep(.8);conn=connect();time.sleep(.3);exchange(b'\r')
    assert snapshot()==initial
    assert 'uart7 RX28 TX29' in exchange(b'io bu\t\r')
    cmd('lcd key ctrlc');r['passed']=True
    print('PASS: board reservations, GPIO/UART conflicts, slot CS ownership, protected I2C rejection, shared COM suspend/resume/close, disconnect cleanup, exact memory recovery')
finally:
    if conn:
        try:
            if sid:cmd('close '+str(sid))
            cmd('io release')
        except Exception as error:r['cleanup_error']=str(error)
        conn.close()
    save()
