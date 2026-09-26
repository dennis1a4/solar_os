"""Fetch a small plain HTTP response into a NEW SD file.
Usage: python http_fetch.py [host [port [path [output]]]]
Run `network up` first and wait for `network status` to show DHCP=bound.
"""
import socket
import sys

host = sys.argv[1] if len(sys.argv) > 1 else 'example.com'
port = int(sys.argv[2]) if len(sys.argv) > 2 else 80
path = sys.argv[3] if len(sys.argv) > 3 else '/'
output = sys.argv[4] if len(sys.argv) > 4 else '/http-response.txt'
if '\r' in host or '\n' in host or '\r' in path or '\n' in path:
    raise ValueError('invalid HTTP host/path')
address = socket.getaddrinfo(host, port, socket.AF_INET, socket.SOCK_STREAM)[0][-1]
response = bytearray()
with socket.socket() as connection:
    connection.settimeout(5)
    connection.connect(address)
    request = 'GET {} HTTP/1.0\r\nHost: {}\r\nConnection: close\r\n\r\n'.format(path, host)
    connection.sendall(request.encode())
    while True:
        chunk = connection.recv(512)
        if not chunk:
            break
        if len(response) + len(chunk) > 32768:
            raise ValueError('response exceeds 32 KiB limit')
        response.extend(chunk)
if not response.startswith(b'HTTP/') or b'\r\n\r\n' not in response:
    raise ValueError('incomplete HTTP response headers')
# Includes headers. Exclusive creation preserves any existing destination.
with open(output, 'xb') as f:
    f.write(response)
print('Saved', len(response), 'bytes to', output)
