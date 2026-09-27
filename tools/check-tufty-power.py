#!/usr/bin/env python3
"""Exercise the production boot/shutdown path with simulated pins and power hardware."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = root / 'drivers/tufty_power.c'
headers = ['pico/stdlib.h', 'hardware/clocks.h', 'hardware/powman.h',
           'hardware/resets.h', 'hardware/structs/usb.h', 'hardware/sync.h',
           'hardware/watchdog.h']
main = (root / 'src/main.c').read_text().split('int main() {', 1)[1]
assert main.index('tufty_power_boot_check();') < main.index('mutex_init(')
with tempfile.TemporaryDirectory(prefix='apple-power-') as directory:
    p = Path(directory)
    for name in headers:
        file = p / name
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text('#include "test-tufty-power-platform.h"\n')
    # Register bit identities are sufficient for the host mock. Firmware builds
    # separately compile against the SDK's real register definitions.
    names = sorted(set(re.findall(r'\b(?:USB_|POWMAN_|RESETS_)[A-Z0-9_]+_BITS\b', source.read_text())))
    (p / 'register-bits.h').write_text('\n'.join(
        f'#define {name} (1u << {i % 31})' for i, name in enumerate(names)))
    executable = p / 'check'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g',
                    '-fsanitize=address,undefined', '-I', str(p),
                    '-I', str(root / 'tests'), '-I', str(root / 'drivers'),
                    '-I', str(root / 'boards'), str(source),
                    str(root / 'tests/tufty_power_test.c'), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
