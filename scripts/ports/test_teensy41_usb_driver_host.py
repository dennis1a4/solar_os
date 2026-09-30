#!/usr/bin/env python3
"""Verify the actual linked USB DMA buffers are in uncached DTCM."""
import argparse
import importlib.util
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--elf', type=Path, default=root / '.pio/build/teensy41_display/firmware.elf')
p.add_argument('--platformio', type=Path, default=Path.home() / '.platformio')
a = p.parse_args()
package = a.platformio / 'packages'
spec = importlib.util.spec_from_file_location('usb_adapter', root / 'scripts/platformio_teensy_usb_storage.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
source = (package / 'framework-arduinoteensy/libraries/USBHost_t36/MassStorageDriver.cpp').read_text()
patched = module.adapt(source)
assert 'while(!msOutCompleted) yield();' not in patched
assert 'while(!msInCompleted) yield();' not in patched
assert 'while (!msControlCompleted) yield();' not in patched
nm = package / 'toolchain-arm-cortexm-linux/bin/arm-cortexm7f-eabi-nm'
symbols = subprocess.check_output([str(nm), '-S', '-C', str(a.elf)], text=True)
buffers = []
for line in symbols.splitlines():
    if ('USBDrive::' in line and ('::CommandBlockWrapper' in line or '::StatusBlockWrapper' in line)) or line.endswith('(anonymous namespace)::drive'):
        address, size, kind, name = line.split(maxsplit=3)
        address, size = int(address, 16), int(size, 16)
        assert kind.lower() in ('b', 'd'), line
        assert 0x20000000 <= address < address + size <= 0x20080000, line
        buffers.append(name)
assert len(buffers) >= 9, buffers
print(f'PASS: pinned MSC adaptation and {len(buffers)} linked USB buffers/driver in DTCM')
