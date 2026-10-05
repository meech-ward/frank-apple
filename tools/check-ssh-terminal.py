#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run the production SSH terminal adapter against a host-side ANSI terminal."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="apple-ssh-terminal-") as directory:
    executable = str(Path(directory) / "check")
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
        "-fsanitize=address,undefined", "-I", str(root / "src"),
        str(root / "src/ssh_terminal.c"), str(root / "tests/ssh_terminal_test.c"),
        "-o", executable,
    ], check=True)
    subprocess.run([executable], check=True, timeout=30)
