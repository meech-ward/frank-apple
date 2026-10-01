#!/usr/bin/env python3
"""Read-only catalog/bitstream tests. Optionally pass a private saved .bdsk file."""
from pathlib import Path
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='apple-catalog-') as directory:
    exe = str(Path(directory) / 'check')
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g',
                    '-fsanitize=address,undefined', '-I', str(root/'src'),
                    str(root/'src/dos_catalog.c'), str(root/'tests/dos_catalog_test.c'),
                    '-o', exe], check=True)
    subprocess.run([exe, *sys.argv[1:]], check=True, timeout=30)
