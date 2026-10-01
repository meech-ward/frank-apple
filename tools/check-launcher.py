#!/usr/bin/env python3
"""Execute the production menu with simulated storage/input. Optional output directory for screen renders."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='apple-launcher-') as directory:
    p=Path(directory)
    # Copy unchanged production source so its quoted platform includes resolve
    # to mocks. The menu, renderer, public headers and command builder are real.
    shutil.copy2(root/'src/disk_ui.c',p/'disk_ui.c')
    for name in ('board_config.h','pico.h','pico/stdlib.h','pico/mutex.h',
                 'hardware/sync.h','mii.h','mii_sw.h','mii_bank.h','debug_log.h','web_control.h'):
        f=p/name;f.parent.mkdir(parents=True,exist_ok=True)
        f.write_text('#include "launcher_test_platform.h"\n')
    exe=str(p/'check')
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-g',
                    '-fsanitize=address,undefined','-I',str(p),'-I',str(root/'tests'),
                    '-I',str(root/'src'),str(p/'disk_ui.c'),str(root/'src/dos_catalog.c'),
                    str(root/'tests/launcher_test.c'),'-o',exe],check=True)
    if len(sys.argv)>1:Path(sys.argv[1]).mkdir(parents=True,exist_ok=True)
    subprocess.run([exe,*sys.argv[1:]],check=True,timeout=30)
