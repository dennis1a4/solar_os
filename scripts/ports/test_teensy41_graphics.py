#!/usr/bin/env python3
"""RA8875 graphics acceptance test. Needs pyserial/Pillow; keep keyboard idle.
Leaves a unique SD demo folder with PNG/JPEG and a Python drawing example.
"""
import argparse,io,json,re,time,uuid
from pathlib import Path
import serial
from serial.tools import list_ports
from PIL import Image,ImageDraw
from test_teensy41_shell import ANSI
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--log',type=Path,required=True);p.add_argument('--mandelbrot',action='store_true');p.add_argument('--mandelbrot-only',action='store_true');a=p.parse_args()
report={'passed':False,'commands':[]};folder='/sd/graphics-demo-'+uuid.uuid4().hex[:8];report['folder']=folder
ports=[p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x16c0,0x0483)];assert len(ports)==1,ports
conn=serial.Serial(ports[0],115200,timeout=.03,write_timeout=3,exclusive=True)
def cmd(s,timeout=45,python=False):
    conn.write((s+'\r').encode());data=bytearray();end=time.monotonic()+timeout
    while time.monotonic()<end:
        data.extend(conn.read(16384));text=ANSI.sub('',data.decode(errors='replace')).replace('\r','')
        assert 'Fault IRQ:' not in text,text
        if (text.endswith('>>> ') if python else re.search(r'[\w.-]+@[\w.-]+:/[^\n]* $',text)):
            report['commands'].append({'input':s,'output':text});a.log.write_text(json.dumps(report,indent=2)+'\n');return text
    raise RuntimeError((s,data[-2500:]))
def py(code):
    out=cmd('python -c '+json.dumps(code));assert 'Traceback' not in out and 'shell:' not in out,out;return out
def upload(name,data):
    path=folder+'/'+name;cmd('python',python=True)
    cmd('import binascii;f=open('+repr(path)+',"wb")',python=True)
    for at in range(0,len(data),256):
        out=cmd('f.write(binascii.unhexlify('+repr(data[at:at+256].hex())+'))',python=True)
        assert 'Traceback' not in out,out
    cmd('f.close()',python=True);cmd('\x04');return path

def local(s):cmd('lcd send '+json.dumps(s));time.sleep(.7)
def status(want,after=None):
    deadline=time.monotonic()+15
    while time.monotonic()<deadline:
        out=cmd('lcd');frames=int(re.search(r'frames=(\d+)',out).group(1))
        if 'graphics='+want in out and (after is None or frames>after):
            report['last_render_ms']=int(re.search(r'render-ms=(\d+)',out).group(1));return frames
        time.sleep(.2)
    raise AssertionError(out)

def close():cmd('lcd key exit');time.sleep(.4);status('idle')
def mem():
    out=cmd('mem');m=re.search(r'Internal heap: (\d+) free / \d+ bytes; PSRAM: (\d+) free',out);assert m,out;return tuple(map(int,m.groups()))
try:
    time.sleep(.5);cmd('\x1d');cmd('lcd key exit');time.sleep(.4)
    if not a.mandelbrot_only:
        apps=cmd('apps');assert 'view -' in apps and 'invaders -' in apps,apps
        assert 'display' in cmd('view /sd/missing.png')
        cmd('mkdir '+folder)
        im=Image.new('RGB',(320,192));d=ImageDraw.Draw(im)
        for i,c in enumerate(['red','green','blue','yellow','cyan','magenta','white','black']):d.rectangle((i*40,0,i*40+39,191),fill=c)
        d.text((8,8),'SolarOS PNG / JPEG',(255,255,255),stroke_width=1,stroke_fill=(0,0,0))
        images=[]
        for ext,fmt in [('png','PNG'),('jpg','JPEG')]:
            b=io.BytesIO();im.save(b,format=fmt);images.append(upload('color-bars.'+ext,b.getvalue()))
        bad=upload('broken.png',b'\x89PNG\r\n\x1a\ntruncated')
        demo='''from solaros import gfx
import solaros
gfx.begin()
gfx.clear(gfx.WHITE)
gfx.color(gfx.rgb(0,80,180))
gfx.fill_rect(10,10,780,60)
gfx.color(gfx.WHITE)
gfx.font(gfx.FONT_BOLD_20)
gfx.text(30,50,"SolarOS graphics on Teensy 4.1")
gfx.color(gfx.rgb(200,0,0))
gfx.fill_circle(150,200,80)
gfx.color(gfx.BLACK)
gfx.rect(270,120,200,160)
gfx.line(270,120,470,280)
gfx.icon(550,150,"folder",64)
gfx.sprite(650,150,8,8,bytes((24,60,60,126,24,36,66,0)))
gfx.present()
while not solaros.should_exit():
    key=gfx.getch(100)
    if key in (ord('q'),gfx.KEY_ESCAPE): break
gfx.end()
'''
        script=upload('drawing.py',demo.encode());report['images']=images;report['drawing']=script
        # USB cannot claim the local user's display, even by name.
        out=cmd('python -c '+json.dumps('from solaros import gfx;gfx.begin("display0")'));assert 'RuntimeError' in out,out
        local('view '+images[0]);status('active');close();baseline=mem()
        for path in images*2:
            before=status('idle');local('view '+path);status('active',after=before)
            assert 'USB_DURING_VIEW' in cmd('echo USB_DURING_VIEW')
            cmd('lcd send "1"');time.sleep(.3);cmd('lcd send "f"');time.sleep(.3);close()
        local('view '+bad);status('idle');assert 'view:' in cmd('lcd dump')
        after=mem();assert after[0]>=baseline[0] and after[1]==baseline[1],(baseline,after)
        local('python '+script);status('active');assert 'USB_DURING_GRAPHICS' in cmd('echo USB_DURING_GRAPHICS');close()
        local('python -c '+json.dumps('from solaros import gfx;gfx.begin();raise ValueError("cleanup-test")'));status('idle');assert 'cleanup-test' in cmd('lcd dump')
        local('invaders');frames=status('active');time.sleep(2);assert status('active')>frames;close()
        # Measure a full redraw, identical redraw and moving small object in Python.
        bench_path=folder+'/timing.txt'
        bench="""from solaros import gfx
from solaros.time import ticks_ms
import solaros
gfx.begin()
times=[]
for x in (10,10,20):
    gfx.clear(gfx.WHITE)
    gfx.color(gfx.BLACK)
    gfx.fill_rect(x,100,8,8)
    start=ticks_ms()
    gfx.present()
    times.append(ticks_ms()-start)
with open(%r,'w') as f: f.write(str(times))
while not solaros.should_exit():
    if gfx.getch(100)==ord('q'): break
gfx.end()
""" % bench_path
        benchmark=upload('benchmark.py',bench.encode())
        local('python '+benchmark);status('active');time.sleep(3)
        timing=cmd('cat '+bench_path)
        values=re.findall(r'\[(\d+), (\d+), (\d+)\]',timing);assert values,timing
        report['python_present_ms']=list(map(int,values[-1]))
        assert report['python_present_ms'][1]<250 and report['python_present_ms'][2]<250,report
        local('q');status('idle')
        # Warm both native/Python paths, then verify repeated ownership and cleanup.
        baseline=mem()
        for _ in range(5):
            local('python '+script);status('active');local('q');status('idle')
            local('invaders');first=status('active');time.sleep(2)
            last=status('active');assert last>first
            report['invaders_render_ms']=report['last_render_ms']
            local('f');status('active');cmd('lcd key esc');status('idle')
        after=mem();assert after[0]>=baseline[0] and after[1]==baseline[1],(baseline,after)
        report['lifecycle_memory']={'before':baseline,'after':after}
        # Single image plus parent entry: j + Enter opens the image in View.
        child_dir=folder+'/child';cmd('mkdir '+child_dir);cmd('cp '+images[0]+' '+child_dir+'/image.png')
        local('files '+child_dir);assert 'image.png' in cmd('lcd dump')
        local('j');status('active');close()
        assert 'image.png' in cmd('lcd dump');close()
        report['files_view_return']=True
        local('plot uptime --rate 250');status('active');close()
        report['core_graphics_passed']=True
    if a.mandelbrot or a.mandelbrot_only:
        cmd('rtc set '+str(int(time.time())));cmd('network up')
        time.sleep(3)
        local('playground refresh')
        deadline=time.monotonic()+90
        while time.monotonic()<deadline:
            refreshed=cmd('lcd dump')
            if 'catalog refreshed' in refreshed.lower():break
            assert 'failed:' not in refreshed.lower() and 'operation failed' not in refreshed.lower(),refreshed
            time.sleep(1)
        else:raise AssertionError(refreshed)
        close()
        installed=cmd('playground install mandelbrot-python',timeout=90)
        assert 'installed' in installed.lower() or 'already' in installed.lower(),installed
        before=status('idle');local('playground run mandelbrot-python');first=status('active',after=before)
        deadline=time.monotonic()+600;last=first
        while time.monotonic()<deadline:
            time.sleep(3);frames=status('active')
            assert 'USB_MANDELBROT' in cmd('echo USB_MANDELBROT')
            last=frames
            # Official example presents initial clear, each of 480 rows, and final frame.
            if last>=before+482:break
        else:raise AssertionError('Mandelbrot did not finish rendering')
        report['mandelbrot_frames']=last-before
        local('q');status('idle')
        report['mandelbrot_passed']=True
    if not a.mandelbrot_only:
        local('view '+images[0]);status('active')
    report['passed']=True
    print('PASS:',{k:v for k,v in report.items() if k!='commands'})
finally:
    conn.close();a.log.write_text(json.dumps(report,indent=2)+'\n')
