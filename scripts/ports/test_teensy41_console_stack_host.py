#!/usr/bin/env python3
"""Check that the linked console stack cannot consume the DTCM network heap."""
import argparse
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--elf', type=Path, default=root / '.pio/build/teensy41_telnet_legacy/firmware.elf')
parser.add_argument('--platformio', type=Path, default=Path.home() / '.platformio')
parser.add_argument('--stack-bytes', type=int, default=40960)
args = parser.parse_args()
nm = args.platformio / 'packages/toolchain-arm-cortexm-linux/bin/arm-cortexm7f-eabi-nm'
symbols = subprocess.check_output([str(nm), '-S', '-C', str(args.elf)], text=True)
stack = heap = None
for line in symbols.splitlines():
    fields = line.split()
    if fields[-1] == 'console_stack':
        stack = (int(fields[0], 16), int(fields[1], 16))
    elif fields[-1] == '_heap_start':
        heap = int(fields[0], 16)
assert stack is not None and heap is not None, 'Missing console stack/heap symbols'
address, size = stack
assert size == args.stack_bytes, (size, args.stack_bytes)
assert address % 8 == 0, hex(address)
assert 0x20200000 <= address < address + size <= heap <= 0x20280000, (stack, heap)
print(f'PASS: {size}-byte console stack in OCRAM, outside DTCM and the allocator pool')
