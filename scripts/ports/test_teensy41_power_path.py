#!/usr/bin/env python3
"""Check the power page on USB; offline setting checks require --expect-unconnected.
Does not enable USB power, change PD, assert KILL, or test live overloads.
"""
import argparse
import re
import time
import serial
from serial.tools import list_ports
from test_teensy41_ftp import Console


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--expect-unconnected', action='store_true')
    parser.add_argument('--reboot-check', action='store_true', help='Check saved pending limit after reboot; requires --expect-unconnected')
    args = parser.parse_args()
    if args.reboot_check and not args.expect_unconnected:
        parser.error('--reboot-check requires --expect-unconnected')
    ports = [p.device for p in list_ports.comports() if (p.vid, p.pid) == (0x16c0, 0x0483)]
    assert len(ports) == 1, ports
    with serial.Serial(ports[0], 115200, timeout=.03, write_timeout=3, exclusive=True) as usb:
        c = Console(usb, {'commands': []})
        c.command('')
        status = c.command('powerpath status')
        print(status, flush=True)
        assert 'MCP23008=0x20 INA237=0x40 STUSB4500=0x28' in status, status
        if args.expect_unconnected:
            assert 'Meter: unconnected' in status and 'Inputs: unconnected' in status, status
            original = int(re.search(r'Limit desired=(\d+) mA', status)[1])
            try:
                for value in ('99', '5001', '-1', 'x', '999999999999999999999999'):
                    out = c.command('powerpath max ' + value)
                    assert 'usage:' in out, out
                out = c.command('powerpath max 1234')
                assert 'pending:' in out and '; saved.' in out, out
                out = c.command('powerpath status')
                assert 'Max 1234mA pending' in out and 'verified=0' in out, out
                assert 'USB host: unconnected' in c.command('usbpower status')
                if args.reboot_check:
                    # Keep DTR asserted until the shell has consumed reboot.
                    # Closing immediately can leave it queued until the next open.
                    usb.reset_input_buffer()
                    usb.write(b'reboot\r')
                    usb.flush()
                    reply = bytearray()
                    acknowledged = False
                    until = time.monotonic() + 30
                    while time.monotonic() < until:
                        try:
                            reply.extend(usb.read(16384))
                        except serial.SerialException:
                            break
                        acknowledged |= b'rebooting' in reply
                    else:
                        raise TimeoutError('Teensy did not disconnect after reboot: ' + repr(reply))
                    assert acknowledged, repr(reply)
                    usb.close()
                    until = time.monotonic() + 60
                    while True:
                        candidates = [p.device for p in list_ports.comports() if (p.vid, p.pid) == (0x16c0, 0x0483)]
                        if len(candidates) == 1:
                            usb.port = candidates[0]
                            try:
                                usb.open()
                                c = Console(usb, {'commands': []})
                                c.command('')
                                out = c.command('powerpath status')
                                break
                            except serial.SerialException:
                                usb.close()
                        assert time.monotonic() < until, 'Teensy USB did not return'
                        time.sleep(.2)
                    assert 'Max 1234mA pending' in out and 'Limit desired=1234 mA' in out, out
                    print('PASS: saved limit restored after reboot, still pending while absent', flush=True)
            finally:
                out = c.command('powerpath max ' + (str(original) if original else 'off'))
                assert '; saved.' in out, out
            print('PASS: absent hardware, strict limit parsing, saved pending setting and restore', flush=True)
        c.command('lcd small page power')
        time.sleep(1.3)
        out = c.command('lcd small')
        print(out, flush=True)
        assert 'small=on' in out and 'Power path' in out, out
        assert 'spi-misses=0' in out, out
        if args.expect_unconnected:
            for line in ('Meter: unconnected', 'Inputs: unconnected', 'PD: unconnected', 'USB host: unconnected'):
                assert line in out, out
        print('PASS: live small-display Power page', flush=True)


if __name__ == '__main__':
    main()
