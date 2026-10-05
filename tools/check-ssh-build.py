#!/usr/bin/env python3
"""Check stack placement and RAM headroom in an SSH-enabled firmware image."""
import argparse
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('build', type=Path)
args = parser.parse_args()
build = args.build.resolve()
cache = (build/'CMakeCache.txt').read_text()
assert 'NETCARD_SSH:BOOL=ON' in cache, 'SSH must be enabled for this check'
nm = re.search(r'^CMAKE_NM:FILEPATH=(.+)$', cache, re.M)
assert nm, 'Missing toolchain symbol reader'
images = list(build.glob('*.elf'))
assert len(images) == 1, 'Expected one firmware ELF'
symbols = {}
for line in subprocess.check_output([nm.group(1), '-n', str(images[0])], text=True).splitlines():
    parts = line.split()
    if len(parts) == 3:
        try: symbols[parts[2]] = int(parts[0], 16)
        except ValueError: pass
stack = symbols['__StackTop'] - symbols['__StackBottom']
headroom = symbols['__HeapLimit'] - symbols['__end__']
assert symbols['__StackTop'] == 0x20080000, 'Core 0 stack must live in main SRAM'
assert stack >= 12288, 'SSH signing requires the expanded core-0 stack'
assert symbols['__HeapLimit'] == symbols['__StackBottom'], 'Heap must exclude the stack'
assert symbols['__scratch_x_end__'] <= symbols['__StackOneBottom'], 'Core 1 scratch overlap'
assert symbols['__StackOneTop'] - symbols['__StackOneBottom'] == 2048, 'Keep core-1 stack separate'
assert headroom >= 64*1024, 'Insufficient static headroom for HTTPS/runtime allocations'
print(f'PASS: {build.name}: core-0 stack {stack} bytes; heap headroom {headroom} bytes; core-1 scratch isolated.')
print('This checks linker reservations, not peak live HTTPS/Wi-Fi allocation or physical stack high-water.')
