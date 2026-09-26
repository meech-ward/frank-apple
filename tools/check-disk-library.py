#!/usr/bin/env python3
"""Run production disk import against real FatFs with ASan/UBSan, no device."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='apple2-disks-') as directory:
    p=Path(directory);(p/'pico').mkdir()
    (p/'pico/time.h').write_text('#include <stdint.h>\nuint64_t time_us_64(void);\n')
    fat=root/'drivers/fatfs'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Wno-unused-parameter','-g','-fsanitize=address,undefined','-I',str(p),'-I',str(root/'src'),'-I',str(fat),str(root/'tools/test-disk-library.c'),str(root/'src/disk_library.c'),*[str(fat/name) for name in ['ff.c','ffsystem.c','ffunicode.c']],'-o',str(p/'check')],check=True)
    subprocess.run([str(p/'check')],check=True)
