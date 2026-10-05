#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run the production SmartPort storage adapter with fault-injected FatFs I/O."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="apple2-smartport-") as directory:
    path = Path(directory)
    # Keep the real board configuration, emulator-bank and FatFs headers. Only
    # unused hardware declarations are stubbed; no storage code is rewritten.
    (path / "hardware/structs").mkdir(parents=True)
    (path / "pico.h").write_text("#pragma once\ntypedef unsigned int uint;\n")
    for header in ["hardware/structs/sysinfo.h", "hardware/vreg.h", "hardware/gpio.h"]:
        (path / header).write_text("#pragma once\n")
    executable = path / "check"
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-Wno-unused-parameter", "-Wno-unused-value", "-Wno-address",
        "-g", "-fsanitize=address,undefined", "-DPICO_RP2350=1",
        "-DMII_RP2350=1", "-DBOARD_TUFTY=1",
        "-I", str(path), "-I", str(root / "src"),
        "-I", str(root / "drivers"), "-I", str(root / "drivers/fatfs"),
        str(root / "tools/test-smartport-storage.c"),
        str(root / "src/mii_dd_stub.c"), "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True, timeout=30)
