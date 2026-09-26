#!/usr/bin/env python3
"""Run the production HTTP parser tests with memory/undefined-behaviour sanitizers."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="badge-http-") as directory:
    exe = str(Path(directory) / "check")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
                    "-fsanitize=address,undefined", "-I", str(root / "src"),
                    str(root / "src/net_http.c"),
                    str(root / "tools/test-net-http.c"), "-o", exe], check=True)
    subprocess.run([exe], check=True)
