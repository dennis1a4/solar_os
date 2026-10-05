#!/usr/bin/env python3
"""Audit every installed app/command manual via its actual USB pager.

Requires idle consoles and exclusive USB serial access (pyserial + pyte).
Reads help only: no application hardware is started and no files/settings are
changed, apart from normal shell history. Also checks one LCD page via injection.
"""
import argparse
import json
import re
import sys
import time
from pathlib import Path

import pyte
from install_teensy41_python_bundle import Board

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import generate_manual as generator
import teensy41_manual as selection


def normalized(text):
    return ' '.join(text.split())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', type=Path, required=True)
    args = parser.parse_args()
    report = {'passed': False, 'pages': []}
    pages = selection.select_pages(generator.load_pages(ROOT / 'doc/manual',
        ROOT / 'packages/solar_os_packages.toml'), generator.markdown_to_terminal_text)
    by_id = {p['id']: p for p in pages}
    board = Board()
    pager_open = False
    lcd_open = False
    try:
        settings = board.command('setterm')
        dims = re.search(r'size (\d+) (\d+)', settings)
        assert dims, settings
        screen = pyte.Screen(int(dims[1]), int(dims[2]))
        stream = pyte.Stream(screen)

        def draw(raw):
            board.c.write(raw)
            data = bytearray()
            deadline = time.monotonic() + 10
            last = time.monotonic()
            while time.monotonic() < deadline:
                chunk = board.c.read(8192)
                if chunk:
                    data.extend(chunk)
                    stream.feed(chunk.decode(errors='replace'))
                    last = time.monotonic()
                elif data and time.monotonic() - last > .2:
                    text = '\n'.join(screen.display)
                    assert 'Fault IRQ:' not in data.decode(errors='replace')
                    return text
            raise TimeoutError(bytes(data)[-1000:])

        installed = {}
        for kind, command in (('app', 'apps'), ('command', 'commands')):
            output = board.command(command)
            installed[kind] = set(re.findall(r'^([a-z0-9-]+) - ', output, re.M))
            assert installed[kind], output
        listed = board.command('man --list')
        ids = set(re.findall(r'^((?:app|command)\.[a-z0-9-]+)\s', listed, re.M))
        expected = {kind + '.' + name for kind, names in installed.items() for name in names}
        assert ids == expected, {'missing': sorted(expected - ids), 'extra': sorted(ids - expected)}
        report['installed'] = {kind: sorted(names) for kind, names in installed.items()}
        report['status'] = board.command('help status')
        assert f'{len(ids)} embedded Teensy' in report['status'], report['status']
        report['memory_before'] = board.command('mem')
        for index, topic in enumerate(sorted(ids)):
            page = by_id[topic]
            # Explicit IDs exercise every entry, including app.help/command.help.
            pager_open = True
            top = draw(('man ' + topic + '\r').encode())
            assert 'man ' + topic in top, (topic, top)
            body_lines = [line.strip() for line in page['body'].splitlines() if line.strip()]
            first = normalized(body_lines[1])[:55]
            assert first in normalized(top), (topic, first, top)
            bottom = draw(b'G')
            last = normalized(body_lines[-1])[-55:]
            assert last in normalized(bottom), (topic, last, bottom)
            for obsolete in ('connect Wi-Fi', 'downloadable manual', 'manual entry not found'):
                assert obsolete not in top + bottom, (topic, obsolete)
            board.c.write(b'q')
            board.read()
            pager_open = False
            report['pages'].append({'topic': topic, 'top': top, 'bottom': bottom})
            if (index + 1) % 20 == 0:
                print(f'Checked {index + 1}/{len(ids)} pages', flush=True)
        # Bare-name lookup, search and browser are distinct paths.
        for name in ('synth', 'webradio', 'arecord', 'lcd', 'sshkey'):
            pager_open = True
            assert 'man ' in draw(('man ' + name + '\r').encode())
            board.c.write(b'q'); board.read(); pager_open = False
        search = board.command('man -k microphone')
        assert 'app.arecord' in search, search
        report['search'] = search
        pager_open = True
        browser = draw(b'help\r')
        assert 'Applications' in browser or 'Commands' in browser, browser
        board.c.write(b'q'); board.read(); pager_open = False
        # Only inject after checking LCD is at its idle shell prompt.
        lcd = board.command('lcd dump')
        lcd_lines = [line.strip() for line in lcd.rsplit('\n', 1)[0].splitlines() if line.strip()]
        assert lcd_lines and re.fullmatch(r'[\w.-]+@[\w.-]+:/\S*', lcd_lines[-1]), lcd
        board.command('lcd send "man synth"'); lcd_open = True
        time.sleep(.3)
        lcd = board.command('lcd dump')
        assert 'man app.synth' in lcd and 'SGTL5000' in lcd, lcd
        report['lcd'] = lcd
        board.command('lcd key exit'); lcd_open = False
        report['memory_after'] = board.command('mem')
        report['passed'] = True
        print(f'PASS: {len(installed["app"])} apps and {len(installed["command"])} commands; '
              'all pages, start/end text, bare aliases, search, browser and LCD pager', flush=True)
    finally:
        if pager_open:
            board.c.write(b'\x1d')
        if lcd_open:
            try:
                board.command('lcd key exit')
            except Exception:
                pass
        board.close()
        args.log.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
