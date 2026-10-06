#!/usr/bin/env python3
"""FTP startup/failure coexistence with an existing SD music folder.

Requires idle USB/LCD consoles, pyserial/pyte, mounted SD and a reachable
anonymous FTP server. Music and scan directories are read only during this test.
Use a large --local directory to exercise decoder starvation during listing.
"""
import argparse
import json
import re
import socket
import threading
import time
from pathlib import Path

import pyte
import serial
from serial.tools import list_ports
from test_teensy41_ftp import Console
from test_teensy41_hotplug import ANSI, PROMPT, require


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--music', required=True)
    p.add_argument('--local', default='/sd')
    p.add_argument('--host', required=True)
    p.add_argument('--cycles', type=int, default=10)
    p.add_argument('--log', type=Path, required=True)
    args = p.parse_args()
    report = dict(passed=False, commands=[], screens=[], checks=[])
    player_id = None
    ftp_open = False
    servers = []
    threads = []
    stop_servers = threading.Event()

    def save():
        args.log.write_text(json.dumps(report, indent=2) + '\n')

    ports = [p.device for p in list_ports.comports() if (p.vid, p.pid) == (0x16c0, 0x0483)]
    require(len(ports) == 1, ports)
    with serial.Serial(ports[0], 115200, timeout=.02, write_timeout=3, exclusive=True) as usb:
        c = Console(usb, report)
        screen = pyte.Screen(80, 24)
        stream = pyte.Stream(screen)

        def ui(raw=b'', expected=None, prompt=False, timeout=20):
            if raw:
                usb.write(raw)
            data = bytearray()
            started = last = time.monotonic()
            while time.monotonic() - started < timeout:
                chunk = usb.read(16384)
                if chunk:
                    data.extend(chunk)
                    stream.feed(chunk.decode(errors='replace'))
                    last = time.monotonic()
                text = ANSI.sub('', data.decode(errors='replace')).replace('\r', '')
                require('Fault IRQ:' not in text and 'STACK OVERFLOW' not in text and 'ASSERT in' not in text, text)
                ready = bool(PROMPT.search(text)) if prompt else time.monotonic() - last > .2
                matches = expected is None or expected in screen.display[22]
                if ready and matches:
                    report['screens'].append(dict(input=repr(raw), screen='\n'.join(screen.display)))
                    return time.monotonic() - started
            report['screens'].append(dict(input=repr(raw), screen='\n'.join(screen.display), raw=text))
            save()
            raise TimeoutError('\n'.join(screen.display))

        def audio():
            out = c.command('audio status')
            m = re.search(r'stereo blocks=(\d+) underruns=(\d+)', out)
            require(m and 'running=1' in out, out)
            require(int(m[2]) == 0, out)
            return int(m[1])

        def close_ftp():
            nonlocal ftp_open
            elapsed = ui(b'\x1d', prompt=True, timeout=4)
            ftp_open = False
            require(elapsed < 2, elapsed)
            return elapsed

        def launch(command, expected, timeout=20):
            nonlocal ftp_open
            ftp_open = True
            return ui((command + '\r').encode(), expected=expected, timeout=timeout)

        def passed(name, **details):
            report['checks'].append(dict(name=name, **details))
            save()
            print('PASS:', name, details, flush=True)

        c.command('')
        initial = c.command('sessions')
        require(not re.search(r'\b(?:active|suspended)\s+', initial), initial)
        previous_dir = re.search(r'usb-shell\s+attached\s+shell\s+(\S+)', initial)[1]
        try:
            c.command('cd ' + json.dumps(args.local))
            ui(('player --repeat one ' + json.dumps(args.music) + '\r').encode())
            ui(b'\x1a', prompt=True)
            sessions = c.command('sessions')
            match = re.search(r'^(\d+)\s+usb-shell\s+suspended\s+player\b', sessions, re.M)
            require(match, sessions)
            player_id = match[1]
            time.sleep(2)
            for cycle in range(args.cycles):
                audio()
                launch('ftp ' + args.host, 'connected')
                close_ftp()
                audio()
                passed('MP3 plus FTP startup', cycle=cycle + 1)
            # Use the small music folder to isolate network failure handling.
            c.command('cd ' + json.dumps(args.music))
            board_ip = re.search(r'address=([\d.]+)', c.command('network status'))[1]
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as route:
                route.connect((board_ip, 9))
                host_ip = route.getsockname()[0]
            for mode, expected in [('refused', 'connection refused'),
                                   ('drop', 'server closed connection'),
                                   ('timeout', 'server response timed out'),
                                   ('cancel', 'connecting...')]:
                server = socket.socket()
                server.bind((host_ip, 0))
                port = server.getsockname()[1]
                servers.append(server)
                if mode == 'refused':
                    server.close()
                else:
                    server.listen(1)
                    server.settimeout(15)

                    def serve(listener=server, kind=mode):
                        try:
                            peer, _ = listener.accept()
                            with peer:
                                if kind == 'drop':
                                    # Let the board observe ESTABLISHED before
                                    # EOF; an immediate close may instead look
                                    # like a refused connect to its RPC poller.
                                    time.sleep(.25)
                                else:
                                    stop_servers.wait(30)
                        finally:
                            listener.close()

                    thread = threading.Thread(target=serve, daemon=True)
                    thread.start()
                    threads.append(thread)
                elapsed = launch(f'ftp {host_ip} {port}', expected, timeout=15)
                if mode == 'timeout':
                    require(elapsed < 13, elapsed)
                cancel_elapsed = close_ftp()
                audio()
                passed('MP3 plus ' + mode, result_seconds=elapsed, exit_seconds=cancel_elapsed)
            report['passed'] = True
        finally:
            try:
                if ftp_open:
                    close_ftp()
                if player_id:
                    c.command('close ' + player_id)
                    time.sleep(.4)
                c.command('cd ' + json.dumps(previous_dir))
                report['final_audio'] = c.command('audio status')
                report['final_sessions'] = c.command('sessions')
            finally:
                stop_servers.set()
                for server in servers:
                    server.close()
                for thread in threads:
                    thread.join(timeout=1)
                save()


if __name__ == '__main__':
    main()
