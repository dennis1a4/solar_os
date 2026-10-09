"""Compare mmuszkow/2048-gb input before/after the source patch (ROMs not bundled)."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--original', type=Path, required=True)
parser.add_argument('--fixed', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
for path in (args.original, args.fixed):
    rom = path.read_bytes()
    assert len(rom) == 32768 and rom[0x143] in (0, 0x80), 'DMG-compatible 32 KiB ROM required'
    assert rom[0x14d] == (-sum(rom[0x134:0x14d]) - 25) & 255, 'Invalid header checksum'
with tempfile.TemporaryDirectory() as tmp:
    binary = Path(tmp) / 'input-test'
    subprocess.run(['cc', '-std=c11', '-O2', '-I', str(root/'src'),
                    str(root/'tests/host/gameboy_2048_input_test.c'), '-o', str(binary)], check=True)
    original = subprocess.run([str(binary), str(args.original.resolve())], capture_output=True, text=True)
    print(original.stdout.splitlines()[-1])
    assert original.returncode == 1, 'Expected missed legal moves in original ROM'
    fixed = subprocess.run([str(binary), str(args.fixed.resolve())], capture_output=True, text=True)
    print(fixed.stdout)
    assert fixed.returncode == 0, fixed.stderr
print('PASS: all 96 legal moves accepted at five taps/second after fixing the ROM input loop')
