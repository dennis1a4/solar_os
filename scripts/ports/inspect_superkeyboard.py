#!/usr/bin/env python3
"""Record schematic nets and spreadsheet rows without modifying hardware files.

Export first with kicad-cli sch export netlist --format kicadxml.
Only the Python standard library is required.
"""
import argparse
import hashlib
import json
import re
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path


def read_pins(path):
    ns = {'t': 'urn:oasis:names:tc:opendocument:xmlns:table:1.0'}
    with zipfile.ZipFile(path) as archive:
        root = ET.fromstring(archive.read('content.xml'))
    pins = {}
    for sheet in root.findall('.//t:table', ns):
        if sheet.get('{%s}name' % ns['t']) != 'T4_1':
            continue
        for row in sheet.findall('t:table-row', ns):
            cells = []
            for cell in row:
                count = int(cell.get('{%s}number-columns-repeated' % ns['t'], 1))
                cells.extend([' '.join(cell.itertext()).strip()] * min(count, 20))
                if len(cells) >= 5:
                    break
            if cells and cells[0].isdigit():
                pins[int(cells[0])] = {'description': cells[1] if len(cells) > 1 else '',
                                      'label': cells[2] if len(cells) > 2 else ''}
    return pins


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--netlist', type=Path, required=True)
    parser.add_argument('--pins', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = ET.parse(args.netlist).getroot()
    pins = read_pins(args.pins)
    for net in root.findall('./nets/net'):
        nodes = net.findall('node')
        for node in nodes:
            match = re.match(r'^(\d+)_', node.get('pinfunction', ''))
            if node.get('ref') != 'U1' or not match:
                continue
            number = int(match[1])
            pins.setdefault(number, {}).update({
                'schematic_net': net.get('name'),
                'symbol_pad': node.get('pin'),
                'connections': [f"{peer.get('ref')}.{peer.get('pin')}" for peer in nodes
                                if peer is not node],
            })
    report = {
        'schematic_source': root.findtext('./design/source'),
        'pins_source': str(args.pins),
        'pins_sha256': hashlib.sha256(args.pins.read_bytes()).hexdigest(),
        'pins': {str(k): v for k, v in sorted(pins.items())},
    }
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(f'Recorded {len(pins)} pin assignments in {args.output}')


if __name__ == '__main__':
    main()
