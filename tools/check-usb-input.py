#!/usr/bin/env python3
"""Exercise production USB input callbacks/translation with real TinyUSB HID definitions."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sdk', type=Path, default=os.environ.get('PICO_SDK_PATH'))
args = parser.parse_args()
candidates = [args.sdk] if args.sdk else [parent / 'pico-sdk' for parent in root.parents]
sdk = next((p for p in candidates if p and (p / 'lib/tinyusb/src/class/hid/hid.h').is_file()), None)
if sdk is None:
    parser.error('provide --sdk or PICO_SDK_PATH pointing to a Pico SDK with TinyUSB')
with tempfile.TemporaryDirectory(prefix='apple-usb-input-') as directory:
    exe = str(Path(directory) / 'check')
    subprocess.run([
        'cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
        '-g', '-fsanitize=address,undefined',
        '-I', str(root / 'tools/usb-input-stubs'),
        '-I', str(sdk / 'lib/tinyusb/src'),
        '-I', str(root / 'drivers'), '-I', str(root / 'drivers/usbhid'),
        '-I', str(root / 'src'),
        str(root / 'drivers/usbhid/apple_usb_input.c'),
        str(root / 'drivers/usbhid/hid_app.c'),
        str(root / 'src/input_controls.c'),
        str(root / 'tools/test-usb-input.c'), '-o', exe,
    ], check=True)
    subprocess.run([exe], check=True)
