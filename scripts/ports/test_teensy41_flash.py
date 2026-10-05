#!/usr/bin/env python3
"""Check fitted QSPI flash and SD/flash interoperability through the shared shell.
Only --initialize-blank permits initialization, and firmware scans every byte
and refuses nonblank media. All ordinary writes use unique test directories.
"""
from teensy41_fixture_cleanup import cleanup_fixtures
import argparse
import hashlib
import json
import re
import time
import termios
import uuid
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_shell import ANSI, PROMPT

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--keep-fixtures', action='store_true', help='Retain successful test files for inspection/persistence checks')
p.add_argument('--log',type=Path,required=True)
p.add_argument('--probe',action='store_true',help='Only status, mount and read-only blank scan')
p.add_argument('--initialize-blank',action='store_true')
p.add_argument('--verify-existing',help='Test directory name from a previous run')
p.add_argument('--reboot',action='store_true')
p.add_argument('--audio-fixtures',help='SD directory from the generated audio test')
a=p.parse_args()
if a.verify_existing and not re.fullmatch(r'_solaros_flash_[0-9a-f]{10}',a.verify_existing): p.error('invalid test directory')
if a.audio_fixtures and not re.fullmatch(r'/_solaros_audio_[0-9a-f]{10}',a.audio_fixtures): p.error('invalid audio fixture directory')
if a.reboot and not a.verify_existing: p.error('--reboot requires --verify-existing')
name=a.verify_existing or '_solaros_flash_'+uuid.uuid4().hex[:10]
sd='/sd/'+name
flash='/flash/'+name
payload=bytes(range(256))*257+b'SolarOS flash\n'
digest=hashlib.sha256(payload).hexdigest()
report={'passed':False,'directory':name,'commands':[]}
try:
    ports=[v.device for v in list_ports.comports() if (v.vid,v.pid)==(0x16c0,0x0483)]
    assert len(ports)==1,ports
    with serial.Serial(ports[0],115200,timeout=.02,write_timeout=3,exclusive=True) as conn:
        time.sleep(1); conn.reset_input_buffer()
        def exchange(raw,suffix=PROMPT,expected=None,quiet=False,timeout=120):
            conn.write(raw); data=bytearray(); deadline=time.monotonic()+timeout; last=time.monotonic()
            while time.monotonic()<deadline:
                chunk=conn.read(8192)
                if chunk: data.extend(chunk); last=time.monotonic()
                text=ANSI.sub('',data.decode(errors='replace')).replace('\r','')
                if (quiet and time.monotonic()-last>.3) or (not quiet and text.endswith(suffix)):
                    report['commands'].append({'input':repr(raw),'output':text})
                    if expected is not None: assert expected in text,text
                    return text
                if 'Fault IRQ:' in text:
                    report['commands'].append({'input':repr(raw),'output':text})
                    raise RuntimeError('Firmware fault: '+text[-1500:])
            raise RuntimeError(f'Timeout: {data[-1500:]!r}')
        def cmd(s,expected=None): return exchange((s+'\r').encode(),expected=expected)
        def py(s):
            text=exchange((s+'\r').encode(),suffix='>>> ')
            assert 'Traceback' not in text,text
            return text
        def block(s): return py('exec('+repr(s)+')')
        def check(path):
            py('f=open('+repr(path)+',"rb"); h=hashlib.sha256(f.read()).digest(); f.close(); assert binascii.hexlify(h).decode()=='+repr(digest))
        def absent(path):
            block('try:\n f=open('+repr(path)+')\n f.close()\n raise AssertionError("file still exists")\nexcept OSError:\n pass')
        conn.write(b'\x03'); time.sleep(.15)
        if conn.read(8192).decode(errors='replace').endswith('>>> '): exchange(b'\x04')
        # Recover a previous directory without assuming it has disappeared.
        exchange(b'cd /\r')
        if a.reboot:
            conn.write(b'reboot\r'); conn.flush(); conn.close(); time.sleep(2)
            deadline=time.monotonic()+25
            while True:
                ports=[v.device for v in list_ports.comports() if (v.vid,v.pid)==(0x16c0,0x0483)]
                if len(ports)==1:
                    conn.port=ports[0]
                    try:
                        conn.open(); time.sleep(1); conn.reset_input_buffer(); break
                    except (serial.SerialException, OSError, termios.error): conn.close()
                assert time.monotonic()<deadline,'USB did not reappear'; time.sleep(.2)
            cmd('')
        status=cmd('flash status','16777216 bytes')
        listing=cmd('ls /')
        assert re.findall(r'<DIR> ([^\n]+)',listing)==['sd/','flash/'],listing
        assert len(listing.splitlines()[1:-1])==2,listing
        if a.probe:
            report['status']=status
            report['mount']=cmd('flash mount')
            report['scan']=cmd('flash scan')
        else:
            initialized=False
            if 'unmounted at /flash' in status:
                assert a.initialize_blank,'Flash unmounted; initialization not requested'
                cmd('flash scan','Flash is completely blank')
                status=cmd('flash init'); initialized=True
            assert ' mounted at /flash' in status,status
            if a.verify_existing:
                exchange(b'python\r',suffix='>>> ')
                py('import hashlib, binascii')
                for path in (sd+'/original.bin',sd+'/returned.bin',flash+'/copied.bin'):
                    check(path)
                exchange(b'\x04')
                cmd('python '+flash+'/hello.py','Hello from flash')
                cmd('flash init','refused')
            else:
                for root in (sd,flash):
                    assert 'mkdir:' not in cmd('mkdir '+root)
                exchange(b'python\r',suffix='>>> ')
                py('import hashlib, binascii, gc')
                py('payload=bytes(range(256))*257+b"SolarOS flash\\n"; f=open('+repr(sd+'/original.bin')+',"xb"); assert f.write(payload)==len(payload); f.close()')
                check('/'+name+'/original.bin')
                check('/flash/../sd/'+name+'/original.bin')
                exchange(b'\x04')
                assert 'cp:' not in cmd('cp '+sd+'/original.bin '+flash+'/copied.bin')
                cmd('cp '+sd+'/original.bin '+flash+'/copied.bin','cp:')
                assert 'cp:' not in cmd('cp '+flash+'/copied.bin '+sd+'/returned.bin')
                assert 'cp:' not in cmd('cp '+flash+'/copied.bin '+flash+'/move.bin')
                assert 'mv:' not in cmd('mv '+flash+'/move.bin '+sd+'/moved.bin')
                assert 'mv:' not in cmd('mv '+sd+'/moved.bin '+flash+'/moved-back.bin')
                # Renaming through an alias of the same SD path is a no-op.
                assert 'mv:' not in cmd('mv '+sd+'/original.bin /'+name+'/original.bin')
                exchange(b'python\r',suffix='>>> ')
                py('import hashlib, binascii, gc')
                for path in (sd+'/original.bin',sd+'/returned.bin',flash+'/copied.bin',flash+'/moved-back.bin'):
                    check(path)
                absent(sd+'/moved.bin'); absent(flash+'/move.bin')
                # Large reads/writes pass PSRAM-backed Python buffers directly
                # to LittleFS. Exercise both directions beyond the CPU cache.
                block('for i in range(20):\n f=open('+repr(flash+'/copied.bin')+',"rb"); data=f.read(); f.close()\n assert binascii.hexlify(hashlib.sha256(data).digest()).decode()=='+repr(digest)+'\n f=open('+repr(flash+'/stress.bin')+',"wb"); assert f.write(data)==len(data); f.close()\n del data\n gc.collect()\n f=open('+repr(flash+'/stress.bin')+',"rb"); data=f.read(); f.close()\n assert binascii.hexlify(hashlib.sha256(data).digest()).decode()=='+repr(digest)+'\n del data\n gc.collect()')
                py('print("PSRAM flash read/write stress passed: 20 cycles")')
                # Native file calls must remain safe at Python's recursion limit.
                block('def deep_file_io():\n try:\n  deep_file_io()\n except RuntimeError:\n  f=open('+repr(flash+'/recursion.txt')+',"w"); f.write("stack reserve works"); f.close()\ndeep_file_io()')
                py('f=open('+repr(flash+'/recursion.txt')+'); assert f.read()=="stack reserve works"; f.close()')
                py('p='+repr(flash+'/modes.txt'))
                py('f=open(p,"w+"); assert f.write("abc")==3; f.seek(0); assert f.read()=="abc"; f.close()')
                py('f=open(p,"a+"); f.seek(0); f.write("def"); f.flush(); f.seek(0); assert f.read()=="abcdef"; f.close()')
                py('f=open(p,"r+"); f.seek(1); f.write("Z"); f.close(); f=open(p); assert f.read()=="aZcdef"; f.close()')
                block('try:\n open(p,"x")\n raise AssertionError("exclusive create")\nexcept OSError:\n pass')
                py('f=open(p,"w"); f.write("x"); f.close(); f=open(p); assert f.read()=="x"; f.close()')
                # Exhaust descriptors without truncating the protected file.
                block('files=[open(p) for _ in range(16)]\ntry:\n open(p,"w")\n raise AssertionError("descriptor exhaustion")\nexcept OSError:\n pass\nfor f in files: f.close()\nf=open(p); assert f.read()=="x"; f.close()')
                block('for i in range(20):\n f=open(p,"r+")\n f.seek(0); assert f.read()=="x"\n f.close()\ngc.collect()')
                exchange(b'\x04')
                cmd('ls '+flash,'copied.bin')
                # Mount root protection must reject before traversing children.
                if initialized: cmd('rm -r /flash','refusing to remove root')
                cmd('flash init','refused')
                # Native editor and interpreter use flash through the same file APIs.
                exchange(('edit '+flash+'/hello.py\r').encode(),quiet=True)
                exchange(b'print("Hello from flash")\r',quiet=True)
                exchange(b'\x13',quiet=True,expected='saved'); exchange(b'\x1d')
                cmd('python '+flash+'/hello.py','Hello from flash')
                exchange(('edit '+flash+'/hello.py\r').encode(),quiet=True)
                exchange(b'\x01# saved twice\rprint("Hello from flash")\r\x13',quiet=True,expected='saved'); exchange(b'\x1d')
                cmd('python '+flash+'/hello.py','Hello from flash')
                exchange(b'cd /flash\r',suffix='user@teensy41:/flash/ ')
                cmd('cd ..')
                assert 'mkdir:' not in cmd('mkdir '+flash+'/empty')
                assert 'rm:' not in cmd('rm -r '+flash+'/empty')
                assert 'rm:' not in cmd('rm '+flash+'/moved-back.bin')
                report['memory_before']=cmd('mem')
                for i in range(5):
                    exchange(b'python\r',suffix='>>> ')
                    py('files=[open('+repr(flash+'/copied.bin')+') for _ in range(4)]')
                    exchange(b'\x04')
                report['memory_after']=cmd('mem')
                assert report['memory_before']==report['memory_after']
            if a.audio_fixtures and not a.verify_existing:
                for audio in ('stereo.mp3','mono48.mp3','mono22.wav'):
                    assert 'cp:' not in cmd('cp '+a.audio_fixtures+'/'+audio+' '+flash+'/'+audio)
                    cmd('aplay -v 10 '+flash+'/'+audio,'aplay: done')
            report['status']=cmd('flash status','open=0')
        report['uptime']=cmd('uptime')
        if not a.probe and not a.verify_existing:
            assert int(re.search(r'stack-free=(\d+)',report['uptime'])[1])>=1024,report['uptime']
        if not a.probe and not a.verify_existing:
            cleanup_fixtures(cmd, (sd, flash), report, a.keep_fixtures)
        report['passed']=True
        print('PASS: '+('read-only flash probe' if a.probe else 'SD/flash file operations and persistence' if a.verify_existing else 'SD/flash copy/move, file modes, editor, Python, cleanup and protection'))
        print(name)
finally:
    a.log.write_text(json.dumps(report,indent=2)+'\n')
