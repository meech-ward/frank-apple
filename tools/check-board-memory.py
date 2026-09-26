#!/usr/bin/env python3
"""Check PSRAM timing/capacity boundaries without touching a board."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='apple-board-memory-') as directory:
    executable = str(Path(directory) / 'check')
    subprocess.run([
        'cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
        '-g', '-fsanitize=address,undefined', '-I', str(root),
        str(root / 'tests/board_memory_params_test.c'), '-o', executable,
    ], check=True)
    subprocess.run([executable], check=True)
