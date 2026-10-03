#!/usr/bin/env python3
"""Recreate the pinned offline source ZIP from an existing micropython-lib clone."""
import argparse,hashlib,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
manifest=json.loads((Path(__file__).resolve().parents[2]/'lib/teensy41/manifest.json').read_text())
commit=manifest['commit']
subprocess.run(['git','-C',str(a.source),'cat-file','-e',commit+'^{commit}'],check=True)
subprocess.run(['git','-C',str(a.source),'archive','--format=zip','--prefix=micropython-lib/','--output='+str(a.output.resolve()),commit],check=True)
actual=hashlib.sha256(a.output.read_bytes()).hexdigest()
assert actual==manifest['archive']['sha256'],(actual,manifest['archive'])
print(a.output,actual)
