#!/usr/bin/env python3
"""Exercise the shared solaros.net API over Ethernet against local TCP/UDP fixtures.
Requires the PJRC kit on a DHCP LAN and the serial monitor closed. No SD writes.
"""
import argparse
import json
import socket
import socketserver
import threading
import time
from pathlib import Path

import serial
from serial.tools import list_ports
from test_teensy41_shell import ANSI, PROMPT

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--board-ip', default='192.168.1.197')
p.add_argument('--log', type=Path, required=True)
a = p.parse_args()
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as route:
    route.connect((a.board_ip, 9))
    host = route.getsockname()[0]
stop = threading.Event()


class TCP(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(10)
        try:
            payload = bytearray()
            while len(payload) < 4096:
                data = self.request.recv(4096-len(payload))
                if not data:
                    return
                payload.extend(data)
                if payload == b'STALL':
                    stop.wait(120)
                    return
            self.request.sendall(payload)
        except OSError:
            pass


class UDP(socketserver.BaseRequestHandler):
    def handle(self):
        data, sock = self.request
        sock.sendto(data, self.client_address)


class TCPServer(socketserver.ThreadingTCPServer):
    daemon_threads = True


tcp = TCPServer((host, 0), TCP)
udp = socketserver.UDPServer((host, 0), UDP)
udp.max_packet_size = 65535
for server in (tcp, udp):
    threading.Thread(target=server.serve_forever, daemon=True).start()
tcp_port, udp_port = tcp.server_address[1], udp.server_address[1]
result = {'passed': False, 'commands': []}
try:
    ports = [q.device for q in list_ports.comports() if (q.vid, q.pid) == (0x16c0, 0x0483)]
    assert len(ports) == 1, ports
    with serial.Serial(ports[0], 115200, timeout=.02, write_timeout=3, exclusive=True) as conn:
        time.sleep(1)
        conn.reset_input_buffer()

        def exchange(raw, suffix=PROMPT, expected=None, timeout=20):
            conn.write(raw)
            data = bytearray()
            deadline = time.monotonic()+timeout
            while time.monotonic() < deadline:
                data.extend(conn.read(8192))
                text = ANSI.sub('', data.decode(errors='replace')).replace('\r', '')
                if text.endswith(suffix):
                    result['commands'].append({'input': raw.decode(errors='replace'), 'output': text})
                    if expected is not None:
                        assert expected in text, text
                    return text
            raise RuntimeError(f'Timeout: {data[-1500:]!r}')

        def cmd(s, expected=None):
            return exchange((s+'\r').encode(), expected=expected)

        def py(s):
            text = exchange((s+'\r').encode(), suffix='>>> ')
            assert 'Traceback' not in text, text
            return text

        def block(s):
            return py('exec('+repr(s)+')')

        def up():
            cmd('network up', 'started')
            deadline = time.monotonic()+30
            while 'DHCP=bound' not in cmd('network status'):
                assert time.monotonic() < deadline
                time.sleep(1)
            cmd('network interfaces', '(preferred base path)')
            cmd('network routes', 'default via eth0')

        conn.write(b'\x03')
        time.sleep(.15)
        if conn.read(8192).decode(errors='replace').endswith('>>> '):
            exchange(b'\x04')
        cmd('')
        up()
        cmd('network router on', 'no downstream interface')
        cmd('network connect '+host+' '+str(tcp_port), 'connected')
        exchange(b'python\r', suffix='>>> ')
        py('import solaros; import solaros.net; n=solaros.net')
        py("assert n.limits()['session_channels']==4 and n.limits()['global_channels']==8")
        py('h=n.tcp_connect('+repr(host)+','+str(tcp_port)+',3000)')
        py('payload=bytes(range(256))*16; n.tcp_send(h,payload,3000)')
        block("received=bytearray()\nwhile True:\n part=n.tcp_receive(h,4096,3000)\n assert part is not None\n if not part: break\n received.extend(part)\nassert received==payload\nn.close(h)")
        py('u=n.udp_open()')
        # Includes empty datagrams, multi-RPC-sized data, IP fragmentation and truncation.
        block("for size in (0,1,511,512,1024,2048,4096):\n data=bytes(range(256))*(size//256)+b'x'*(size%256)\n n.udp_send(u,"+repr(host)+","+str(udp_port)+",data,3000)\n r=n.udp_receive(u,4096,3000)\n assert r is not None and r['data']==data and not r['truncated']\n assert r['address']=="+repr(host)+" and r['port']=="+str(udp_port)+" and r['datagram_bytes']==size")
        py('n.udp_send(u,'+repr(host)+','+str(udp_port)+',payload,3000)')
        py("r=n.udp_receive(u,7,3000); assert r['data']==payload[:7] and r['truncated'] and r['datagram_bytes']==4096")
        py('assert n.udp_receive(u,10,0) is None')
        start = time.monotonic()
        py('assert n.udp_receive(u,10,150) is None')
        assert time.monotonic()-start < 2
        py('old=u; n.close(u); u=n.udp_open(); assert old!=u')
        block("try:\n n.close(old)\n raise AssertionError('stale handle')\nexcept OSError:\n pass")
        block("try:\n n.tcp_receive(u,1,0)\n raise AssertionError('wrong channel kind')\nexcept OSError:\n pass")
        py('n.close_all(); handles=[n.udp_open() for _ in range(4)]')
        block("try:\n n.udp_open()\n raise AssertionError('session quota')\nexcept OSError:\n pass\nassert n.limits()['open_channels']==4\nn.close_all()")
        # Existing standard socket module and OS channels coexist and clean up separately.
        py('import socket; socks=[socket.socket() for _ in range(4)]; handles=[n.udp_open() for _ in range(4)]')
        py('n.close_all(); [s.close() for s in socks]')
        block("for i in range(20):\n u=n.udp_open()\n n.close(u)\nassert n.limits()['open_channels']==0 and n.limits()['global_open_channels']==0")
        block("for fn,args in ((n.websocket_connect,('ws://example.com',)),(n.ping,('example.com',)),(n.router_start,())):\n try:\n  fn(*args)\n  raise AssertionError('unsupported feature')\n except OSError as e:\n  assert 'NOT_SUPPORTED' in str(e)")
        py('h=n.tcp_connect('+repr(host)+','+str(tcp_port)+',3000); n.tcp_send(h,b"STALL")')
        py('assert n.tcp_receive(h,1,150) is None')
        conn.write(b'n.tcp_receive(h,1,60000)\r')
        time.sleep(.2)
        start = time.monotonic()
        exchange(b'\x03', suffix='>>> ', expected='KeyboardInterrupt')
        assert time.monotonic()-start < 2
        py('n.close_all(); handles=[n.udp_open() for _ in range(4)]')
        # Interpreter exit with live OS channels must destroy the shared session.
        exchange(b'\x04')
        exchange(b'python\r', suffix='>>> ')
        py("import solaros; n=solaros.net; assert n.limits()['global_open_channels']==0")
        py('handles=[n.udp_open() for _ in range(4)]')
        exchange(b'\x04')
        result['memory_before'] = cmd('mem')
        for i in range(5):
            exchange(b'python\r', suffix='>>> ')
            py('import solaros; handles=[solaros.net.udp_open() for _ in range(4)]')
            exchange(b'\x04')
        result['memory_after'] = cmd('mem')
        assert result['memory_before'] == result['memory_after']
        cmd('network down', 'stopped')
        cmd('network routes', 'default unavailable')
        exchange(b'python\r', suffix='>>> ')
        block("import solaros\ntry:\n solaros.net.udp_open()\n raise AssertionError('offline')\nexcept OSError:\n pass")
        exchange(b'\x04')
        up()
        exchange(b'python\r', suffix='>>> ')
        py('import solaros; n=solaros.net; u=n.udp_open(); n.udp_send(u,'+repr(host)+','+str(udp_port)+',b"recovered",3000)')
        py('assert n.udp_receive(u,100,3000)["data"]==b"recovered"; n.close(u)')
        exchange(b'\x04')
        result['status'] = cmd('network status')
        result['passed'] = True
        print('PASS: shared SolarOS registry, TCP/UDP bindings, timeout/cancel, quotas, cleanup and restart')
finally:
    stop.set()
    for server in (tcp, udp):
        server.shutdown()
        server.server_close()
    a.log.write_text(json.dumps(result, indent=2)+'\n')
