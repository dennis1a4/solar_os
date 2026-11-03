#!/usr/bin/env python3
"""Reader/Writer acceptance on RA8875, USB console, Ethernet and mounted SD.
Uses unique documents, the existing FTP service for upload, and local keyboard
injection. Never overwrites user documents. Keep physical keyboard idle.
"""
import argparse,ftplib,io,json,re,socket,time,uuid
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_ftp import Console
from teensy41_document_fixtures import fixtures

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log',type=Path,required=True)
    parser.add_argument('--ftp-port',type=int,default=22121)
    parser.add_argument('--finish-cleanup',action='store_true',help='Resume only cleanup after all functional checks passed in --log')
    args=parser.parse_args();report={'passed':False,'commands':[],'checks':[]}
    root='/sd/_documents_'+uuid.uuid4().hex[:8];report['fixture']=root
    if args.finish_cleanup:
        report=json.loads(args.log.read_text());root=report['fixture']
        assert re.fullmatch(r'/sd/_documents_[0-9a-f]{8}',root)
        assert len(report['checks'])==7 and report['memory_before']==report['memory_after']
        report['cleanup_resumed']=True
    ports=[p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x16c0,0x0483)];assert len(ports)==1,ports
    def passed(name):
        report['checks'].append(name);args.log.write_text(json.dumps(report,indent=2)+'\n');print('PASS:',name,flush=True)
    try:
        with serial.Serial(ports[0],115200,timeout=.03,write_timeout=3,exclusive=True) as usb:
            c=Console(usb,report);c.command('')
            def local(s):assert 'Queued' in c.command('lcd send '+json.dumps(s));time.sleep(.3)
            def key(s):assert 'Queued' in c.command('lcd key '+s);time.sleep(.25)
            def graphics(want,after=None):
                def ready(out):
                    m=re.search(r'frames=(\d+)',out)
                    return 'graphics='+want in out and m and (after is None or int(m[1])>after)
                out=c.poll('lcd status',ready,timeout=20)
                return int(re.search(r'frames=(\d+)',out).group(1))
            def sessions():return c.command('sessions')
            def suspended(name):
                out=sessions();m=re.search(r'^(\d+)\s+lcd-shell\s+suspended\s+'+name+r'\b',out,re.M);assert m,out;return m[1]
            def resume(sid):assert 'Queued' in c.command('fg '+sid);graphics('active')
            def py(code):
                if '\n' in code: code='exec('+repr(code)+')'
                out=c.command('python -c '+json.dumps(code));assert 'Traceback' not in out and 'Fault IRQ:' not in out,out;return out
            def text(path):return py('print(repr(open('+repr(path)+').read()))')
            def memory():
                out=c.command('mem');m=re.search(r'Internal heap: (\d+) free / \d+ bytes; PSRAM: (\d+) free',out);assert m,out;return list(map(int,m.groups()))
            def exit_clean():key('exit');graphics('idle')
            def digest(path):
                value=2166136261
                for b in path.encode():value=((value^b)*16777619)&0xffffffff
                return f'{value:08x}'
            draft=root+'/draft.md'
            if not args.finish_cleanup:
                ready='DOCUMENTS_READY_'+uuid.uuid4().hex[:8]
                local('echo '+ready);c.poll('lcd dump',lambda out:ready in out,timeout=20)
                assert 'ftpd stopped' in c.command('job status ftpd')
                out=c.poll('network status',lambda out:'link=up' in out and 'address=0.0.0.0' not in out)
                host=re.search(r'address=(\d+\.\d+\.\d+\.\d+)',out).group(1)
                c.command('mkdir '+root)
                assert 'ftpd: OK' in c.command(f'job start ftpd {root} {args.ftp_port}')
                ftp=ftplib.FTP();ftp.connect(host,args.ftp_port,timeout=10);ftp.login()
                docs=fixtures();docs['oversize.md']=b'x'*(256*1024+1)
                for name,data in docs.items():ftp.storbinary('STOR '+name,io.BytesIO(data))
                ftp.quit();assert 'ftpd: OK' in c.command('job stop ftpd')
                passed('unique text/Markdown, PNG and compressed EPUB fixtures uploaded')
                # Reader navigation, search, zoom and retained sessions.
                local('reader --pager '+root+'/reading.md');frame=graphics('active')
                key('down');key('+');graphics('active',frame)
                key('/');local('Needle');key('esc');key('end');key('ctrlz');reader=suspended('reader');graphics('idle')
                positions=text('/flash/.reader/positions');assert root+'/reading.md' in positions and 'z=2' in positions,positions
                resume(reader);exit_clean()
                passed('Reader Markdown/images, pager/search/zoom, saved position and suspend/resume')
                local('reader --pager '+root+'/book.epub');graphics('active');key('end');key('down');exit_clean()
                positions=text('/flash/.reader/positions');assert re.search(r'c=1 '+re.escape(root+'/book.epub'),positions),positions
                local('reader '+root+'/book.epub');graphics('active');exit_clean()
                local('reader man:app.writer');graphics('active');exit_clean()
                passed('Reader compressed two-chapter EPUB, saved chapter and graphical offline manual')
                # Writer Ctrl+Z must undo instead of suspending the app.
                draft=root+'/draft.md';local('writer '+draft);graphics('active');key('ctrla');local('First draft');key('ctrlz');key('ctrlz');key('ctrls')
                assert "'Original\\n'" in text(draft)
                assert not re.search(r'suspended\s+writer\b',sessions())
                key('ctrly');key('ctrly');key('ctrls');assert "'First draft\\n'" in text(draft)
                key('ctrlr');local('First');local('Final');key('ctrls');assert "'Final draft\\n'" in text(draft)
                key('ctrla');key('ctrlc');key('ctrlv');key('ctrls');assert "'Final draft\\n'" in text(draft)
                key('f1');key('esc');key('ctrlf');local('draft');key('f3')
                passed('Writer typing, save, Ctrl+Z undo, Ctrl+Y redo, replace, clipboard, format/find dialogs')
                # Replace the selection with unsaved text, persist recovery and close.
                key('ctrla');local('Older snapshot');time.sleep(2.3)
                key('ctrla');local('Recovered draft')
                key('f12');writer=suspended('writer');graphics('idle')
                recovery='/flash/.writer/'+digest(draft)+'.recovery'
                assert 'Recovered draft' in text(recovery)
                assert 'Final draft' in text(draft)
                assert 'Queued' in c.command('close '+writer);time.sleep(.4)
                local('writer '+draft);graphics('active');key('r');key('ctrls');assert "'Recovered draft\\n'" in text(draft)
                exit_clean();passed('Writer F12 suspend and recovery after retained-session close/reopen')
                # Retain both apps together and exercise Files -> Reader -> Files.
                local('reader '+root+'/reading.md');graphics('active');key('ctrlz');reader=suspended('reader')
                local('writer '+draft);graphics('active');key('f12');writer=suspended('writer')
                resume(reader);exit_clean();resume(writer);exit_clean()
                local('files '+root);key('down');key('enter');graphics('active');key('exit');graphics('idle');key('exit')
                passed('Reader/Writer retained together; Files launches Reader and returns to its parent')
                # Guarded error paths must return to a usable shell and free their state.
                local('reader '+root+'/broken.epub');graphics('idle');assert 'reader:' in c.command('lcd dump')
                local('writer '+root+'/oversize.md');graphics('idle');assert '256 KiB' in c.command('lcd dump')
                local('reader '+root+'/missing.md');graphics('idle')
                before=memory()
                for _ in range(3):
                    local('reader '+root+'/reading.md');graphics('active');exit_clean()
                    local('writer '+draft);graphics('active');exit_clean()
                after=memory();report['memory_before']=before;report['memory_after']=after
                assert before[1]==after[1],('PSRAM recovery',before,after)
                passed('malformed/missing EPUB/text and oversized writer files handled; repeated app PSRAM recovery exact')
            # Remove only metadata belonging to our unique document paths.
            py("p='/flash/.reader/positions';lines=open(p).readlines();f=open(p,'w');f.write(''.join(line for line in lines if "+repr(root)+" not in line));f.close()")
            for suffix in ['.meta','.recovery']:
                path='/flash/.writer/'+digest(draft)+suffix
                py("import os\np="+repr(path)+"\ntry: os.remove(p)\nexcept OSError: pass")
            c.command('rm -r '+root)
            local('reader man:app.writer');graphics('active')
            report['passed']=True
    finally:
        args.log.write_text(json.dumps(report,indent=2)+'\n')
    print('PASS: Reader/Writer device acceptance; graphical Writer manual left visible',flush=True)
if __name__=='__main__':main()
