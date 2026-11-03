#!/usr/bin/env python3
"""PDA acceptance on a Teensy with idle LCD, exclusive USB, SD and Ethernet.
Creates unique local fixtures, backs up/restores organizer data, and preserves
pre-existing puzzle saves. Keeps physical keyboard/controllers idle during tests.
"""
import argparse,ftplib,io,json,re,time,uuid
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_ftp import Console

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--log',type=Path,required=True);p.add_argument('--ftp-port',type=int,default=22221);p.add_argument('--reboot',action='store_true',help='Verify nonempty organizer reload across a board reboot');args=p.parse_args()
 tag=uuid.uuid4().hex[:8];root='/sd/_pda_'+tag
 report={'passed':False,'commands':[],'checks':[],'fixture':root};backup=root+'/before.pim';saved_games={};ftp_active=False;current='';backup_ready=False
 ports=[d.device for d in list_ports.comports() if (d.vid,d.pid)==(0x16c0,0x0483)];assert len(ports)==1,ports
 def passed(s):report['checks'].append(s);args.log.write_text(json.dumps(report,indent=2)+'\n');print('PASS:',s,flush=True)
 try:
  with serial.Serial(ports[0],115200,timeout=.03,write_timeout=3,exclusive=True) as usb:
   c=Console(usb,report);c.command('')
   def local(s):assert 'Queued' in c.command('lcd send '+json.dumps(s));time.sleep(.3)
   def key(s):assert 'Queued' in c.command('lcd key '+s);time.sleep(.25)
   def screen():return c.command('lcd dump')
   def expect(s):return c.poll('lcd dump',lambda out:s in out,timeout=20)
   def open_app(name,args_text=''):
    nonlocal current
    local(name+(' '+args_text if args_text else ''));current=name
   def close():
    nonlocal current
    key('exit');time.sleep(.2);current=''
   def input_line(s):local(s)
   def search(s):key('/');local(s)
   def file_operation(key_name,path):key(key_name);local(path);time.sleep(.3)
   def py(code):
    if '\n' in code:code='exec('+repr(code)+')'
    out=c.command('python -c '+json.dumps(code));assert 'Traceback' not in out,out;return out
   def memory():
    out=c.command('mem');m=re.search(r'Internal heap: (\d+) free / \d+ bytes; PSRAM: (\d+) free',out);assert m,out;return list(map(int,m.groups()))
   ready='PDA_READY_'+tag
   for attempt in range(5):
    local('echo '+ready)
    try:c.poll('lcd dump',lambda out:ready in out,timeout=3);break
    except AssertionError:
     if attempt==4:raise
   assert 'ftpd stopped' in c.command('job status ftpd')
   c.command('mkdir '+root)
   for game in ['sudoku','mines']:
    path='/flash/.organizer/'+game+'.bin'
    out=py("import os; print('PDA_EXISTS',"+repr(game+'.bin')+" in os.listdir('/flash/.organizer'))")
    exists='PDA_EXISTS True' in out;saved_games[game]=exists
    if exists:
     assert 'cp:' not in c.command('cp '+path+' '+root+'/old-'+game+'.bin');c.command('rm '+path)
   open_app('calendar');expect('Calendar');file_operation('b',backup);expect('Backup saved');backup_ready=True;report['backup_ready']=True;report['saved_games']=saved_games
   # Read local date/time without changing the clock or timezone.
   date=re.search(r'\b(\d{4}-\d{2}-\d{2})\b',c.command('date')).group(1).replace('-','')
   clock=re.search(r'\b(\d{2}):(\d{2}):(\d{2})\b',c.command('time'));assert clock
   when=date+'T'+clock[1]+clock[2]+'00'
   contact=f'BEGIN:VCARD\r\nVERSION:3.0\r\nUID:pda-{tag}-person\r\nFN:Person{tag}\r\nORG:SolarOS bench\r\nTEL;TYPE=CELL:555-0100\r\nTEL;TYPE=WORK:555-0101\r\nEMAIL:test@example.invalid\r\nLABEL:Bench\\nCanada\r\nBDAY:20000101\r\nCATEGORIES:PDA acceptance\r\nNOTE:Temporary acceptance contact\r\nEND:VCARD\r\n'
   event=f'BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:pda-{tag}-event\r\nSUMMARY:Appointment{tag}\r\nDTSTART:20270131T090000\r\nDTEND:20270131T100000\r\nRRULE:FREQ=MONTHLY\r\nCATEGORIES:PDA acceptance\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n'
   task=f'BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VTODO\r\nUID:pda-{tag}-task\r\nSUMMARY:Task{tag}\r\nDUE;VALUE=DATE:20270131\r\nPRIORITY:1\r\nRRULE:FREQ=MONTHLY;BYMONTHDAY=28,29,30,31;BYSETPOS=-1\r\nEND:VTODO\r\nEND:VCALENDAR\r\n'
   alarm=f'BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:pda-{tag}-alarm\r\nSUMMARY:Reminder{tag}\r\nDTSTART:{when}\r\nBEGIN:VALARM\r\nACTION:DISPLAY\r\nTRIGGER:-PT0M\r\nDESCRIPTION:Test\r\nEND:VALARM\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n'
   bad='BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nSUMMARY:Must not import\r\nDTSTART:20261026T090000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n'
   fixtures={'person.vcf':contact.encode(),'event.ics':event.encode(),'task.ics':task.encode(),'alarm.ics':alarm.encode(),'bad.ics':bad.encode(),f'reference{tag}.md':b'# Bench reference\n\nOffline technical reference test.\n'}
   out=c.poll('network status',lambda out:'link=up' in out and 'address=0.0.0.0' not in out)
   host=re.search(r'address=(\d+\.\d+\.\d+\.\d+)',out).group(1)
   assert 'ftpd: OK' in c.command(f'job start ftpd {root} {args.ftp_port}');ftp_active=True
   f=ftplib.FTP();f.connect(host,args.ftp_port,timeout=10);f.login()
   for name,data in fixtures.items():f.storbinary('STOR '+name,io.BytesIO(data))
   f.quit();assert 'ftpd: OK' in c.command('job stop ftpd');ftp_active=False
   file_operation('i',root+'/event.ics');expect('Imported 1 records');search('Appointment'+tag);expect('Appointment'+tag)
   file_operation('i',root+'/bad.ics');expect('Import rejected');file_operation('o',root+'/events-out.ics');expect('Exported');assert 'Appointment'+tag in c.command('cat '+root+'/events-out.ics')
   key('ctrlz');out=c.command('sessions');m=re.search(r'^(\d+)\s+lcd-shell\s+suspended\s+calendar',out,re.M);assert m,out;cal_id=m[1];current=''
   open_app('contacts');file_operation('i',root+'/person.vcf');expect('Imported 1 records');search('Person'+tag);expect('Person'+tag);key('enter');expect('555-0101');key('esc')
   # Add a personal contact using the actual form, then cancel an edit.
   key('a');local('Form'+tag);key('ctrls');expect('Saved');search('Form'+tag);expect('Form'+tag);key('e');key('ctrlu');local('Cancelled edit');key('esc');expect('Form'+tag)
   file_operation('o',root+'/contacts-out.vcf');expect('Exported');assert '555-0101' in c.command('cat '+root+'/contacts-out.vcf');close()
   assert 'Queued' in c.command('fg '+cal_id);current='calendar';expect('Appointment'+tag);close()
   passed('Calendar recurrence/import validation, personal contact forms/search/vCard and retained sessions')
   open_app('tasks');file_operation('i',root+'/task.ics');expect('Imported 1 records');search('Task'+tag);expect('2027-01-31');key('space');expect('2027-02-28');file_operation('o',root+'/tasks-out.ics');expect('Exported');assert '20270228' in c.command('cat '+root+'/tasks-out.ics');close()
   passed('Recurring prioritized tasks advance from Jan 31 to Feb 28 and export VTODO')
   if args.reboot:
    usb.write(b'reboot\r');usb.flush();usb.close();time.sleep(2)
    deadline=time.monotonic()+40
    while True:
     try:
      candidates=[d.device for d in list_ports.comports() if (d.vid,d.pid)==(0x16c0,0x0483)]
      assert len(candidates)==1,candidates
      usb=serial.Serial(candidates[0],115200,timeout=.03,write_timeout=3,exclusive=True)
      c=Console(usb,report);c.command('');break
     except (OSError,AssertionError,serial.SerialException,TimeoutError):
      try:usb.close()
      except OSError:pass
      if time.monotonic()>deadline:raise
      time.sleep(.3)
    ready='PDA_REBOOT_'+tag
    for attempt in range(5):
     local('echo '+ready)
     try:c.poll('lcd dump',lambda out:ready in out,timeout=3);break
     except AssertionError:
      if attempt==4:raise
    open_app('contacts');search('Person'+tag);expect('Person'+tag);key('enter');expect('555-0101');close()
    open_app('tasks');search('Task'+tag);expect('2027-02-28');close()
    passed('Nonempty organizer, personal contact fields and recurring-task date survive reboot')
   open_app('calendar');file_operation('i',root+'/alarm.ics');expect('Imported 1 records');close()
   c.command('lcd small page system');c.poll('lcd small',lambda out:'REMINDER' in out,timeout=15)
   open_app('calendar');key('K');c.poll('lcd small',lambda out:'REMINDER' not in out,timeout=15)
   file_operation('R',backup);key('y');expect('Backup restored');close()
   passed('Reminders run with Calendar closed, appear on small display, acknowledge and restore backup')
   open_app('convert','32 F C');out=expect(' = ');m=re.search(r'32 F = ([\d.eE+-]+) C',out);assert m and abs(float(m[1]))<1e-9,out;close()
   open_app('convert','1 m kg');expect('different dimensions');close()
   open_app('library',root);expect('Reference library');search('reference'+tag);key('enter');c.poll('lcd status',lambda out:'graphics=active' in out,timeout=20);key('exit');expect('Reference library');close()
   passed('Converter results and dimension errors; reference-library search opens Reader and returns')
   for game in ['sudoku','mines']:
    open_app(game);expect('Sudoku' if game=='sudoku' else 'Minesweeper');key('right');key('down');expect('Row 2 Col 2')
    if game=='sudoku':key('c');expect('conflicting cells')
    else:key('f');key('f');key('enter')
    key('ctrlz');out=c.command('sessions');m=re.search(r'^(\d+)\s+lcd-shell\s+suspended\s+'+game,out,re.M);assert m,out
    assert 'Queued' in c.command('fg '+m[1]);expect('Row 2 Col 2');close();open_app(game);expect('Saved game resumed');expect('Row 2 Col 2');close()
   passed('Sudoku and Minesweeper keyboard play, retained sessions and saved-game resume')
   before=memory()
   for _ in range(2):
    for app in ['calendar','contacts','tasks','convert','sudoku','mines']:
     open_app(app);close()
   after=memory();report['memory_before']=before;report['memory_after']=after;assert before==after,(before,after)
   passed('Repeated organizer/tool/game app cycles recover internal heap and PSRAM exactly')
   # Restore only game files that existed before this test, then remove its own.
   for game,existed in saved_games.items():
    path='/flash/.organizer/'+game+'.bin';c.command('rm '+path)
    if existed:assert 'cp:' not in c.command('cp '+root+'/old-'+game+'.bin '+path)
   py("p='/flash/.reader/positions';a=open(p).readlines();f=open(p,'w');f.write(''.join(x for x in a if "+repr(root)+" not in x));f.close()")
   c.command('rm -r '+root)
   report['small_status']=c.command('lcd small');report['system_status']=c.command('mem')
   open_app('calendar');expect('Calendar');report['passed']=True;usb.close()
 finally:
  if 'usb' in locals():
   try:usb.close()
   except OSError:pass
  if not report['passed'] and backup_ready:
   try:
    with serial.Serial(ports[0],115200,timeout=.03,write_timeout=3,exclusive=True) as cleanup_usb:
     cleanup=Console(cleanup_usb,report);cleanup.command('');cleanup.command('lcd key exit');time.sleep(.3)
     cleanup.command('lcd send "calendar"');cleanup.poll('lcd dump',lambda out:'Calendar' in out)
     cleanup.command('lcd key R');cleanup.command('lcd send '+json.dumps(backup));cleanup.command('lcd key y');cleanup.poll('lcd dump',lambda out:'Backup restored' in out)
     cleanup.command('lcd key exit')
     if ftp_active:cleanup.command('job stop ftpd')
     for game,existed in saved_games.items():
      path='/flash/.organizer/'+game+'.bin';cleanup.command('rm '+path)
      if existed:cleanup.command('cp '+root+'/old-'+game+'.bin '+path)
     report['rollback']='Original organizer and puzzle saves restored; fixtures retained for inspection'
   except Exception as error:report['cleanup_error']=str(error)
  args.log.write_text(json.dumps(report,indent=2)+'\n')
 print('PASS: PDA device acceptance; Calendar left visible',flush=True)
if __name__=='__main__':main()
