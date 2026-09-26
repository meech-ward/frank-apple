#!/usr/bin/env python3
"""Verify supported build inputs exclude the retired driver implementations.

This is a build-scope regression check, not a license determination for every
source or embedded asset. Run after building each supported board/USB profile.
"""
import argparse
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('build', type=Path)
args = parser.parse_args()
build = args.build.resolve()
cache = (build / 'CMakeCache.txt').read_text()
assert 'PS2_KEYBOARD_ENABLED:BOOL=OFF' in cache, 'Check applies to the supported PS/2-off profiles'
commands = json.loads(subprocess.check_output(['ninja', '-C', str(build), '-t', 'compdb'], text=True))
retired = ('/drivers/ps2kbd/', '/drivers/usbhid/usbhid_wrapper.', '/drivers/psram_init.', '/drivers/psram_allocator.')
objects = []
for item in commands:
    source = item.get('file', '')
    assert not any(part in source for part in retired), 'Retired source is compiled: ' + source
    if item.get('output', '').endswith(('.o', '.obj')):
        objects.append(item['output'])
assert objects, 'No compilation objects found'
# Ask Ninja only about active compilation outputs; its database may retain old,
# now-unreachable objects after an incremental configuration change.
deps = subprocess.check_output(['ninja', '-C', str(build), '-t', 'deps', *objects], text=True)
for line in deps.splitlines():
    assert not any(part in line for part in retired), 'Retired driver/header dependency: ' + line.strip()
maps = list(build.glob('*.elf.map'))
assert len(maps) == 1, 'Expected exactly one firmware link map'
linked = maps[0].read_text()
for name in ('libps2kbd.a', 'ps2kbd_wrapper.cpp.o', 'ps2kbd_mrmltr.cpp.o', 'usbhid_wrapper.c.o', 'psram_init.c.o', 'psram_allocator.c.o'):
    assert name not in linked, 'Retired object on link map: ' + name
assert 'input_controls.c.o' in linked
if 'USB_HID_ENABLED:BOOL=ON' in cache:
    assert 'apple_usb_input.c.o' in linked
if 'BOARD_VARIANT:STRING=TUFTY' in cache:
    assert 'board_memory.c.o' in linked and 'vendor/pico_psram/' in deps
print('PASS: ' + build.name + ': active sources, headers and link map exclude the retired USB, PS/2 and memory drivers.')
