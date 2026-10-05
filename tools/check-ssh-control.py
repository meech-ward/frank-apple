#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise production SSH control and terminal with mocked board/transport I/O."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="apple-ssh-control-") as directory:
    temporary = Path(directory)
    shutil.copy2(root / "src/ssh_control.c", temporary / "ssh_control.c")
    for name in ("disk_ui.h", "netcard.h", "debug_log.h", "pico/rand.h", "pico/time.h",
                 "lwip/netif.h", "lwip/ip4_addr.h"):
        path = temporary / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#include "ssh_control_test_platform.h"\n')
    executable = str(temporary / "check")
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
        "-fsanitize=address,undefined", "-I", str(temporary), "-I", str(root / "tests"),
        "-I", str(root / "src"), str(temporary / "ssh_control.c"),
        str(root / "src/ssh_terminal.c"), str(root / "tests/ssh_control_test.c"),
        "-o", executable,
    ], check=True)
    subprocess.run([executable], check=True, timeout=30)
