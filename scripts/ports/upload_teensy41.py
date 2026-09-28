#!/usr/bin/env python3
"""Pinned PJRC loader for Teensy 4.1 images beyond the bundled loader's 1 MiB bug.
Linux host requires make, a C compiler, and libusb-compat development headers.
"""
from pathlib import Path
import subprocess
import sys
root=Path(__file__).resolve().parents[2]
source=root/'.pio/teensy-tools/loader'
revision='03fca4156c244c7ad36bd368cf6e24531dbd566a'
if not source.exists():
    source.parent.mkdir(parents=True,exist_ok=True)
    subprocess.run(['git','clone','--no-checkout','https://github.com/PaulStoffregen/teensy_loader_cli.git',str(source)],check=True)
    subprocess.run(['git','-C',str(source),'checkout','--detach',revision],check=True)
actual=subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()
if actual!=revision:raise SystemExit('Unexpected Teensy loader revision: '+actual)
subprocess.run(['make','-C',str(source)],check=True)
if len(sys.argv)!=2:raise SystemExit('usage: upload_teensy41.py firmware.hex')
subprocess.run([str(source/'teensy_loader_cli'),'--mcu=TEENSY41','-w','-s','-v',sys.argv[1]],check=True,timeout=90)
