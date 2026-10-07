#!/usr/bin/env python3
"""Real Teensy multi-console soak: MP3/Python/FTP/SSH and retained sessions.

Requires idle USB/LCD/Telnet consoles, pyserial and paramiko. Uses a unique SD
fixture, an authenticated temporary FTP/Telnet service, and an isolated host SSH
fixture (no host shell). Existing music is read only. SSH adds one known-host key.
Successful runs remove their SD fixture; failed runs retain evidence.
"""
import argparse
import ftplib
import hashlib
import io
import json
import re
import secrets
import socket
import threading
import time
import uuid
import wave
from pathlib import Path

import paramiko
import serial
from serial.tools import list_ports
from test_teensy41_ftp import Console
from test_teensy41_hotplug import ANSI, PROMPT
from test_teensy41_telnet import Telnet


class SSHFixture:
    def __init__(self, host, password):
        self.password = password
        self.key = paramiko.ECDSAKey.generate()
        self.stop = threading.Event()
        self.transports = []
        self.errors = []
        self.listener = socket.socket()
        self.listener.bind((host, 0))
        self.port = self.listener.getsockname()[1]
        self.listener.listen(4)
        self.listener.settimeout(.2)
        self.thread = threading.Thread(target=self.accept, daemon=True)
        self.thread.start()

    def accept(self):
        while not self.stop.is_set():
            try:
                peer, _ = self.listener.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            threading.Thread(target=self.serve, args=(peer,), daemon=True).start()

    def serve(self, peer):
        password = self.password
        class Server(paramiko.ServerInterface):
            def check_auth_password(self, user, secret):
                return paramiko.AUTH_SUCCESSFUL if user == 'fixture' and secret == password else paramiko.AUTH_FAILED
            def get_allowed_auths(self, user): return 'password'
            def check_channel_request(self, kind, channel):
                return paramiko.OPEN_SUCCEEDED if kind == 'session' else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED
            def check_channel_pty_request(self, *args): return True
            def check_channel_env_request(self, *args): return True
            def check_channel_shell_request(self, *args): return True
        transport = paramiko.Transport(peer)
        self.transports.append(transport)
        transport.add_server_key(self.key)
        try:
            transport.start_server(server=Server())
            channel = transport.accept(30)
            if channel is None:
                return
            channel.settimeout(.2)
            channel.sendall(b'fixture ready\r\n$ ')
            line = bytearray()
            while not self.stop.is_set() and transport.is_active():
                try:
                    raw = channel.recv(4096)
                except socket.timeout:
                    continue
                if not raw:
                    break
                for byte in raw:
                    if byte == 13:
                        command = line.decode(errors='replace')
                        line.clear()
                        if command == 'exit':
                            channel.send_exit_status(0)
                            channel.close()
                            return
                        if command == 'drop':
                            return
                        result = '0123456789abcdef' * 256 if command == 'bulk' else 'ACK:' + command
                        channel.sendall(('\r\n' + result + '\r\n$ ').encode())
                    elif byte != 10:
                        line.append(byte)
        except (OSError, EOFError, paramiko.SSHException) as error:
            self.errors.append(repr(error))
        finally:
            transport.close()

    def close(self):
        self.stop.set()
        self.listener.close()
        for transport in self.transports:
            transport.close()
        self.thread.join(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--probe', action='store_true')
    parser.add_argument('--music', default='/sd/music')
    parser.add_argument('--seconds', type=int, default=180)
    parser.add_argument('--cycles', type=int, default=4)
    parser.add_argument('--payload-kib', type=int, default=256, help='Largest FTP payload; intervening transfers use one quarter this size')
    parser.add_argument('--upload-delay-ms', type=int, default=0)
    parser.add_argument('--upload-block', type=int, default=8192)
    parser.add_argument('--record-seconds', type=int, default=0)
    parser.add_argument('--mode', choices=('all', 'no-python', 'no-ssh', 'no-audio', 'network-only', 'audio-ssh', 'ssh-only'), default='all')
    parser.add_argument('--soak-only', action='store_true')
    args = parser.parse_args()
    use_audio = args.mode not in ('no-audio', 'network-only', 'ssh-only')
    use_python = args.mode not in ('no-python', 'network-only', 'audio-ssh', 'ssh-only')
    use_ftp = args.mode not in ('audio-ssh', 'ssh-only')
    use_ssh = args.mode != 'no-ssh'
    assert args.mode == 'all' or args.soak_only, 'Reduced modes require --soak-only'
    report = dict(passed=False, commands=[], checks=[], samples=[], transfers=[], errors=[])
    root = '/sd/_multitask_' + uuid.uuid4().hex[:8]
    report['fixture'] = root
    report['mode'] = args.mode
    report['parameters'] = {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()}
    report['script_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    secret = secrets.token_hex(12)
    remote = ssh = None
    ftp_started = telnet_started = fixture_created = False
    owned = set()
    transfer_stop = threading.Event()
    transfer_thread = None
    transfer_errors = []
    phase = 'setup'
    started = time.monotonic()

    def save():
        report['elapsed_seconds'] = round(time.monotonic() - started, 3)
        args.log.write_text(json.dumps(report, indent=2) + '\n')

    def passed(name, **details):
        report['checks'].append(dict(name=name, **details))
        print('PASS:', name, details, flush=True)
        save()

    ports = [p.device for p in list_ports.comports() if (p.vid, p.pid) == (0x16c0, 0x0483)]
    assert len(ports) == 1, ports
    with serial.Serial(ports[0], 115200, timeout=.03, write_timeout=3, exclusive=True) as usb:
        c = Console(usb, report)
        def cmd(command):
            try:
                return c.command(command)
            finally:
                save()

        def raw(data, predicate=None, seconds=10):
            usb.write(data)
            output = bytearray()
            begin = time.monotonic()
            while time.monotonic() - begin < seconds:
                output.extend(usb.read(16384))
                text = ANSI.sub('', output.decode(errors='replace')).replace('\r', '')
                assert not any(m in text for m in ('Fault IRQ:', 'STACK OVERFLOW', 'ASSERT in')), text
                if predicate and predicate(text):
                    break
            report['commands'].append(dict(input=repr(data), output=text, seconds=time.monotonic()-begin))
            save()
            if predicate:
                assert predicate(text), text[-3000:]
            return text

        def memory():
            out = cmd('mem')
            m = re.search(r'Internal heap: (\d+) free / \d+ bytes; PSRAM: (\d+) free', out)
            assert m, out
            return list(map(int, m.groups()))

        def audio():
            if not use_audio:
                return 0
            out = cmd('audio status')
            m = re.search(r'stereo blocks=(\d+) underruns=(\d+)', out)
            assert m and 'running=1 paused=0' in out, out
            assert int(m[2]) == 0, out
            return int(m[1])

        def connect_ftp():
            f = ftplib.FTP()
            f.connect(board_ip, 2121, timeout=20)
            f.login('test', secret)
            return f

        def telnet_login():
            r = Telnet(board_ip, 2323)
            r.read(lambda s: 'Password: ' in s)
            r.send(secret.encode() + b'\r\n')
            r.read(lambda s: bool(PROMPT.search(s)))
            return r

        def remote_cmd(command):
            begin = time.monotonic()
            out = remote.command(command)
            report['commands'].append(dict(console='telnet', command=command, output=out, seconds=time.monotonic()-begin))
            save()
            return out

        def ssh_login():
            remote.send(f'ssh fixture@{ssh_host} {ssh.port}\r\n'.encode())
            out = remote.read(lambda s: s.endswith(': '), timeout=30)
            remote.send(secret.encode() + b'\r\n')
            out += remote.read(lambda s: s.endswith('$ ') or bool(PROMPT.search(s)), timeout=45)
            report['commands'].append(dict(console='telnet', command='ssh login', output=out))
            save()
            assert 'fixture ready' in out, out

        def ssh_command(command):
            then = time.monotonic()
            remote.send(command.encode() + b'\r')
            out = remote.read(lambda s: s.endswith('$ '), timeout=15)
            expected = '0123456789abcdef'*256 if command == 'bulk' else 'ACK:' + command
            assert expected in out, out[-300:]
            return time.monotonic() - then

        def ssh_exit(drop=False):
            remote.send(b'drop\r' if drop else b'exit\r')
            remote.read(lambda s: bool(PROMPT.search(s)), timeout=15)

        def start_player():
            cmd('lcd send ' + json.dumps('player --repeat all ' + json.dumps(args.music)))
            time.sleep(2)
            cmd('lcd key ctrlz')
            time.sleep(.25)
            out = cmd('sessions')
            match = re.search(r'^(\d+)\s+lcd-shell\s+suspended\s+player\b', out, re.M)
            assert match, out
            sid = int(match[1])
            owned.add(sid)
            audio()
            return sid

        def start_python():
            out = raw(('python ' + root + '/load.py\r').encode(), seconds=.7)
            assert 'Traceback' not in out, out
            out = raw(b'\x1a', lambda s: bool(PROMPT.search(s)))
            match = re.search(r'Suspended session (\d+)', out)
            assert match, out
            sid = int(match[1])
            owned.add(sid)
            assert 'background' in cmd('bg ' + str(sid)), out
            return sid

        def close_session(sid):
            cmd('close ' + str(sid))
            deadline = time.monotonic() + 8
            while re.search(r'^' + str(sid) + r'\s', cmd('sessions'), re.M):
                assert time.monotonic() < deadline, sid
                time.sleep(.2)
            owned.discard(sid)

        def transfer_loop():
            index = 0
            try:
                while not transfer_stop.is_set():
                    begin = time.monotonic()
                    report['transfer_progress'] = dict(index=index, stage='connecting')
                    payload = bytes(range(256)) * (args.payload_kib if index % 3 else args.payload_kib * 4) + index.to_bytes(4, 'little')
                    with connect_ftp() as f:
                        name = 'transfer.bin'
                        report['transfer_progress'] = dict(index=index, stage='uploading', bytes=len(payload))
                        def pace(_):
                            if args.upload_delay_ms:
                                time.sleep(args.upload_delay_ms / 1000)
                        f.storbinary('STOR ' + name, io.BytesIO(payload), blocksize=args.upload_block, callback=pace)
                        report['transfer_progress'] = dict(index=index, stage='uploaded', bytes=len(payload), elapsed=time.monotonic()-begin)
                        result = io.BytesIO()
                        f.retrbinary('RETR ' + name, result.write, blocksize=8192)
                        report['transfer_progress'] = dict(index=index, stage='downloaded', bytes=len(payload), elapsed=time.monotonic()-begin)
                        assert result.getvalue() == payload, 'FTP payload corruption'
                        assert f.size(name) == len(payload)
                        assert name in dict(f.mlsd())
                        f.rename(name, 'checked.bin')
                        f.delete('checked.bin')
                    report['transfers'].append(dict(index=index, phase=phase, bytes=len(payload), seconds=time.monotonic()-begin, sha256=hashlib.sha256(payload).hexdigest()))
                    index += 1
            except BaseException as error:
                transfer_errors.append(repr(error))

        try:
            cmd('')
            initial = cmd('sessions')
            assert not re.search(r'\b(?:active|suspended)\s+', initial), initial
            jobs = cmd('jobs')
            assert not re.search(r'^\d+ python|script[0-3] (?:running|waiting|queued)|ftpd running', jobs, re.M), jobs
            for command in ('network status', 'audio status', 'sd status', 'mem', 'ls ' + args.music):
                print(cmd(command), flush=True)
            if args.probe:
                report['passed'] = True
                return
            assert 'stopped' in cmd('telnetd status')
            board_ip = re.search(r'address=([\d.]+)', cmd('network status'))[1]
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as route:
                route.connect((board_ip, 9))
                host_ip = route.getsockname()[0]
            # Distinct textual IP avoids a pre-existing port-22 known-host entry
            # applying to this isolated test server through libssh2 fallback.
            octets = host_ip.split('.')
            octets[2] = '0' + octets[2]
            ssh_host = '.'.join(octets)
            cmd('mkdir ' + root)
            fixture_created = True
            assert 'ftpd: OK' in cmd(f'job start ftpd {root} 2121 --user test --password {secret}')
            ftp_started = True
            # 10,000 integer operations per sample plus a flushed SD log every
            # ~50 ms exercises both CPU scheduling and shared-card contention.
            script = ("import time\nf=open(%r,'w')\ni=0\nwhile True:\n"
                      " x=0\n for j in range(10000): x=(x+j)&65535\n"
                      " f.write('ROW '+str(i)+' '+str(x)+'\\n')\n f.flush()\n"
                      " print('ROW',i)\n i+=1\n time.sleep_ms(50)\n") % (root+'/rows.txt')
            with connect_ftp() as f:
                f.storbinary('STOR load.py', io.BytesIO(script.encode()))
                f.storbinary('STOR telnet.pass', io.BytesIO((secret+'\n').encode()))
            assert 'listening' in cmd(f'telnetd start {root}/telnet.pass 2323')
            telnet_started = True
            remote = telnet_login()
            ssh = SSHFixture(host_ip, secret)
            # Establish host trust before playback: flash programming masks the
            # audio ISR on this platform and is separately documented.
            ssh_login()
            report['ssh_idle_seconds'] = ssh_command('warmup')
            ssh_exit()
            idle_payload = bytes(range(256)) * 256
            with connect_ftp() as f:
                begin_idle = time.monotonic()
                f.storbinary('STOR idle.bin', io.BytesIO(idle_payload))
                upload_idle = time.monotonic() - begin_idle
                begin_idle = time.monotonic()
                downloaded = io.BytesIO()
                f.retrbinary('RETR idle.bin', downloaded.write)
                report['ftp_idle'] = dict(bytes=len(idle_payload), upload_seconds=upload_idle, download_seconds=time.monotonic()-begin_idle)
                assert downloaded.getvalue() == idle_payload
                f.delete('idle.bin')
            passed('idle FTP byte-exact timing baseline', **report['ftp_idle'])
            baseline = memory()
            report['baseline_with_services'] = baseline
            player = start_player() if use_audio else None
            process = start_python() if use_python else None
            phase = args.mode
            if use_ftp:
                transfer_thread = threading.Thread(target=transfer_loop, daemon=True)
                transfer_thread.start()
            else:
                cmd('job stop ftpd')
                ftp_started = False
            report['pre_ssh_memory'] = memory()
            report['pre_ssh_tasks'] = cmd('top')
            if use_ssh:
                ssh_login()
            begin = time.monotonic()
            previous_blocks = audio()
            iteration = 0
            while time.monotonic() - begin < args.seconds:
                assert not transfer_errors, transfer_errors
                if use_ssh:
                    latency = ssh_command('bulk' if iteration % 4 == 0 else 'echo-' + str(iteration))
                else:
                    before_echo = time.monotonic()
                    assert 'ALIVE' in remote_cmd('echo ALIVE')
                    latency = time.monotonic() - before_echo
                blocks = audio()
                if blocks < previous_blocks:
                    report.setdefault('track_counter_resets', []).append(dict(previous=previous_blocks, current=blocks))
                elif use_audio:
                    assert blocks > previous_blocks, (blocks, previous_blocks)
                previous_blocks = blocks
                row = None
                if use_python:
                    out = cmd('tail -n 1 ' + root + '/rows.txt')
                    match = re.search(r'^ROW (\d+) (\d+)$', out, re.M)
                    assert match, out
                    assert int(match[2]) == sum(range(10000)) & 65535, out
                    row = int(match[1])
                report['samples'].append(dict(phase=phase, elapsed=time.monotonic()-begin, blocks=blocks, python_row=row, ssh_seconds=latency, transfers=len(report['transfers'])))
                if iteration % 5 == 0:
                    print('SOAK:', report['samples'][-1], flush=True)
                    cmd('top')
                    cmd('mem')
                iteration += 1
                time.sleep(.4)
            if use_ssh:
                ssh_exit()
            if use_ftp:
                assert len(report['transfers']) >= 1, report['transfers']
            if use_python:
                assert report['samples'][-1]['python_row'] > report['samples'][0]['python_row']
            passed('concurrent workload soak', mode=args.mode, samples=iteration, transfers=len(report['transfers']))
            if args.soak_only:
                transfer_stop.set()
                if transfer_thread:
                    transfer_thread.join(30)
                    assert not transfer_thread.is_alive() and not transfer_errors, transfer_errors
                    transfer_thread = None
                report['passed'] = True
                return
            # Open and detach Calc on USB while the same workload stays active.
            phase = 'retained_calc_disconnect'
            raw(b'calc\r', seconds=.3)
            out = raw(b'123+456\r', seconds=.5)
            assert '579' in out, out
            out = raw(b'\x1a', lambda s: bool(PROMPT.search(s)))
            calc = int(re.search(r'Suspended session (\d+)', out)[1])
            owned.add(calc)
            audio()
            # A dropped Telnet owner must close SSH without affecting detached
            # Python, LCD player, or the independent FTP service.
            ssh_login()
            remote.close()
            remote = None
            time.sleep(1)
            audio()
            remote = telnet_login()
            assert 'TELNET_RECONNECTED' in remote_cmd('echo TELNET_RECONNECTED')
            for cycle in range(args.cycles):
                ssh_login()
                ssh_command('cycle-' + str(cycle))
                ssh_exit(drop=bool(cycle % 2))
                raw(('fg ' + str(calc) + '\r').encode(), seconds=.3)
                raw(b'\x1a', lambda s: bool(PROMPT.search(s)))
                audio()
            passed('retained Calc switching and SSH graceful/abrupt disconnects during playback and FTP', cycles=args.cycles)
            close_session(calc)
            # Move the detached Python process to Telnet, suspend it there,
            # and detach again; the logger must survive Telnet disconnect.
            remote.send(('fg ' + str(process) + '\r\n').encode())
            remote.read(lambda s: 'ROW' in s)
            remote.send(b'\x1a')
            out = remote.read(lambda s: bool(PROMPT.search(s)))
            assert 'Suspended session ' + str(process) in out, out
            assert 'background' in remote_cmd('bg ' + str(process))
            remote.close()
            remote = None
            time.sleep(1)
            audio()
            assert re.search(r'^' + str(process) + r'\s+python\s+running\b', cmd('jobs'), re.M)
            passed('Python reattachment to Telnet and detached disconnect survival')
            transfer_stop.set()
            transfer_thread.join(30)
            assert not transfer_thread.is_alive() and not transfer_errors, transfer_errors
            transfer_thread = None
            close_session(process)
            close_session(player)
            time.sleep(.5)
            # Repeated startup/stop with all four facilities exercises worker
            # admission and teardown, checked against a warmed idle baseline.
            remote = telnet_login()
            warm = memory()
            report['cycle_baseline'] = warm
            for cycle in range(args.cycles):
                player = start_player()
                process = start_python()
                ssh_login()
                ssh_command('cleanup-' + str(cycle))
                with connect_ftp() as f:
                    result = io.BytesIO()
                    f.retrbinary('RETR load.py', result.write)
                    assert result.getvalue() == script.encode()
                ssh_exit()
                audio()
                close_session(process)
                close_session(player)
                deadline = time.monotonic() + 5
                while memory() != warm:
                    assert time.monotonic() < deadline, ('heap mismatch', warm, memory())
                    time.sleep(.2)
                passed('combined workload start/stop recovers exact heap', cycle=cycle+1, memory=warm)
            if args.record_seconds:
                phase = 'recorder_python_ftp_ssh'
                cmd('lcd send ' + json.dumps('recorder ' + root + '/concurrent.wav'))
                time.sleep(.7)
                cmd('lcd key r')
                time.sleep(.7)
                screen = cmd('lcd dump')
                assert 'recording' in screen, screen
                cmd('lcd key ctrlz')
                out = cmd('sessions')
                recorder = int(re.search(r'^(\d+)\s+lcd-shell\s+suspended\s+recorder\b', out, re.M)[1])
                owned.add(recorder)
                assert 'in use' in cmd('player ' + args.music).lower()
                process = start_python()
                transfer_stop.clear()
                transfer_thread = threading.Thread(target=transfer_loop, daemon=True)
                transfer_thread.start()
                ssh_login()
                begin = time.monotonic()
                while time.monotonic() - begin < args.record_seconds:
                    assert not transfer_errors, transfer_errors
                    ssh_command('recording')
                    out = cmd('audio status')
                    assert 'overruns=0' in out, out
                    time.sleep(.5)
                ssh_exit()
                transfer_stop.set()
                transfer_thread.join(30)
                assert not transfer_thread.is_alive() and not transfer_errors, transfer_errors
                transfer_thread = None
                close_session(process)
                close_session(recorder)
                with connect_ftp() as f:
                    data = io.BytesIO()
                    f.retrbinary('RETR concurrent.wav', data.write)
                data.seek(0)
                with wave.open(data, 'rb') as recording:
                    report['recording'] = dict(channels=recording.getnchannels(), rate=recording.getframerate(), width=recording.getsampwidth(), frames=recording.getnframes())
                    assert (recording.getnchannels(), recording.getframerate(), recording.getsampwidth()) == (1, 44100, 2)
                    assert recording.getnframes() >= args.record_seconds * 44100
                assert int.from_bytes(data.getvalue()[40:44], 'little') == len(data.getvalue()) - 44
                passed('retained recording + Python + FTP + SSH produces finalized WAV without capture overruns', **report['recording'])
            report['final_audio'] = cmd('audio status')
            report['final_sessions'] = cmd('sessions')
            report['passed'] = True
        except BaseException as error:
            report['errors'].append(repr(error))
            save()
            print('FAIL:', repr(error), flush=True)
            raise
        finally:
            transfer_stop.set()
            if transfer_thread:
                transfer_thread.join(25)
            if remote:
                remote.close()
            if ssh:
                ssh.close()
                report['ssh_server_errors'] = ssh.errors
            # Close only IDs and services owned by this run. Retain failed SD
            # fixtures for diagnosis; never remove user files or sessions.
            usb_responsive = True
            for sid in list(owned):
                try:
                    cmd('close ' + str(sid))
                except Exception as error:
                    report.setdefault('cleanup_errors', []).append(repr(error))
                    usb_responsive = False
                    break
            for command in (['telnetd stop'] if telnet_started else []) + (['job stop ftpd'] if ftp_started else []):
                if not usb_responsive:
                    break
                try:
                    cmd(command)
                except Exception as error:
                    report.setdefault('cleanup_errors', []).append(repr(error))
                    usb_responsive = False
            if fixture_created and usb_responsive:
                try:
                    if report['passed']:
                        cmd('rm -r ' + root)
                    report['final_sd'] = cmd('sd status')
                    report['final_memory'] = memory()
                    report['final_jobs'] = cmd('jobs')
                except Exception as error:
                    report.setdefault('cleanup_errors', []).append(repr(error))
            if report.get('cleanup_errors') or transfer_errors:
                report['passed'] = False
            report['transfer_errors'] = transfer_errors
            save()


if __name__ == '__main__':
    main()
