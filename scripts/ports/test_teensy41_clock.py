#!/usr/bin/env python3
"""Remote Clock checks. Sets RTC from host UTC and saves Manitoba timezone.
Drives LCD app through USB diagnostics; keep keyboard idle. No visual/audio pass
is implied. --reboot also verifies the saved timezone across a software reboot.
"""
import argparse
import json
import re
import time
import termios
from pathlib import Path
import serial
from serial.tools import list_ports
from test_teensy41_hotplug import Console, require


def connect(report):
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        ports = [p.device for p in list_ports.comports() if (p.vid, p.pid) == (0x16c0, 0x0483)]
        if len(ports) == 1:
            try:
                port = serial.Serial(ports[0], 115200, timeout=.05, write_timeout=3, exclusive=True)
                time.sleep(.5)
                port.reset_input_buffer()
                c = Console(port, report)
                c.command('')
                return c
            except (serial.SerialException, TimeoutError, OSError, termios.error):
                if 'port' in locals():
                    port.close()
        time.sleep(.2)
    raise RuntimeError('Teensy serial did not return')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--log', type=Path, required=True)
    p.add_argument('--reboot', action='store_true')
    args = p.parse_args()
    report = dict(passed=False, commands=[], memory={}, visual_verified=False, audio_verified=False)
    c = None
    try:
        c = connect(report)
        require('display-only' in c.command('clock'), 'USB launch must not claim LCD')
        require('/sd mounted' in c.command('sd status'), 'SD not mounted')
        require('/usb,' in c.command('usb status'), 'Expected test USB drive')
        epoch = int(time.time())
        require('epoch=' in c.command('rtc set ' + str(epoch)), 'RTC set failed')
        require('timezone Manitoba (UTC5)' in c.command('setterm timezone Manitoba'), 'Timezone not saved')

        def memory(label):
            out = c.command('mem')
            m = re.search(r'Internal heap: (\d+) free / (\d+) bytes; PSRAM: (\d+) free / (\d+)', out)
            require(m is not None, out)
            report['memory'][label] = list(map(int, m.groups()))
            return report['memory'][label]

        def status(mode, after=None):
            out = c.poll('lcd', lambda s: 'graphics=' + mode in s and
                         (after is None or int(re.search(r'frames=(\d+)', s).group(1)) > after), timeout=20)
            return int(re.search(r'frames=(\d+)', out).group(1))

        def launch(command):
            c.lcd(command)
            return status('active')

        def close():
            c.command('lcd key exit')
            status('idle')

        memory('idle_before_graphics')
        first = launch('clock')
        status('active', first)
        memory('clock_active')
        close()
        memory('warm_idle')
        first = launch('clock -s')
        c.command('lcd key space')
        status('active', first)
        time.sleep(1.2)
        c.command('lcd key space')
        time.sleep(.7)
        paused = status('active')
        time.sleep(1.3)
        require(status('active') == paused, 'Paused stopwatch kept rendering')
        c.lcd('r')
        status('active', paused)
        close()
        first = launch('clock -a 00:03')
        status('active', first)
        time.sleep(3.5)
        expired = status('active')
        time.sleep(1.3)
        require(status('active') == expired, 'Countdown did not settle at zero')
        report['audio_status'] = c.command('audio status')
        close()
        # A new countdown proves the transient schedule was released on exit.
        launch('clock -a 00:10')
        close()
        for i in range(5):
            launch('clock')
            close()
            memory('cycle_' + str(i + 1))
        before, after = report['memory']['warm_idle'], report['memory']['cycle_5']
        require(after[0] >= before[0] - 2048 and after[2] >= before[2] - 2048, 'Unexpected repeated-launch memory loss')
        report['rtc_before_reboot'] = c.command('rtc')
        if args.reboot:
            c.port.write(b'reboot\r')
            c.port.close()
            c = None
            time.sleep(3)
            c = connect(report)
            require('timezone Manitoba (UTC5)' in c.command('setterm timezone'), 'Timezone did not persist')
            rtc = c.command('rtc')
            value = int(re.search(r'epoch=(\d+)', rtc).group(1))
            require(abs(value - time.time()) < 10, 'RTC did not retain correct UTC over software reboot')
            report['rtc_after_reboot'] = rtc
            require('/sd mounted' in c.command('sd status'), 'SD did not mount after reboot')
            require('/usb,' in c.command('usb status'), 'USB did not mount after reboot')
            memory('idle_after_reboot')
        report['passed'] = True
    except BaseException as exc:
        report['error'] = repr(exc)
        raise
    finally:
        if c:
            try:
                c.command('lcd key exit')
            finally:
                c.port.close()
        args.log.write_text(json.dumps(report, indent=2) + '\n')
        print('PASS' if report['passed'] else 'FAIL', args.log)
        print(json.dumps(report['memory'], indent=2))


if __name__ == '__main__':
    main()
