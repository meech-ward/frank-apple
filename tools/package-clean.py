#!/usr/bin/env python3
"""Prepare a disk-free local install package. No downloads, application disks or device access.

Tufty packages contain a fresh empty FAT16 volume. Installing their full UF2
replaces saved data. Firmware-only UF2s preserve existing disk storage.
See docs/DISTRIBUTION.md before publishing firmware.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
ROOT=Path(__file__).resolve().parents[1]
FLASH_OFFSET=4*1024*1024
FLASH_SIZE=16*1024*1024

def empty_volume(wifi=None):
    """12 MiB FAT16 superfloppy; empty /apple, optional explicit private /wifi.ini."""
    total=24576;sectors_per_cluster=4;fat_sectors=24;root_sectors=32
    data_start=1+2*fat_sectors+root_sectors
    image=bytearray(total*512)
    boot=bytearray(512);boot[:11]=b'\xeb\x3c\x90APPLE2  '
    struct.pack_into('<HBHBHHBHHHII',boot,11,512,sectors_per_cluster,1,2,512,total,0xf8,fat_sectors,32,64,0,0)
    boot[36:39]=bytes([0x80,0,0x29]);struct.pack_into('<I',boot,39,0x41324949)
    boot[43:54]=b'APPLE2     ';boot[54:62]=b'FAT16   ';boot[510:512]=b'\x55\xaa';image[:512]=boot
    fat=bytearray(fat_sectors*512);struct.pack_into('<HHH',fat,0,0xfff8,0xffff,0xffff)
    root=(1+2*fat_sectors)*512;data=data_start*512
    def entry(offset,name,attr,cluster,size=0):
        image[offset:offset+11]=name;image[offset+11]=attr
        struct.pack_into('<H',image,offset+26,cluster);struct.pack_into('<I',image,offset+28,size)
    entry(root,b'APPLE      ',0x10,2)
    entry(data,b'.          ',0x10,2);entry(data+32,b'..         ',0x10,0)
    if wifi is not None:
        if not 0<len(wifi)<=512:raise ValueError('wifi.ini must contain 1–512 bytes')
        struct.pack_into('<H',fat,6,0xffff);entry(root+32,b'WIFI    INI',0x20,3,len(wifi))
        image[data+2048:data+2048+len(wifi)]=wifi
    image[512:512+len(fat)]=fat;image[(1+fat_sectors)*512:(1+fat_sectors)*512+len(fat)]=fat
    assert len(image)==FLASH_SIZE-FLASH_OFFSET
    return bytes(image)

def verify_uf2(data,payload,address=0x10000000):
    rebuilt=bytearray();count=len(data)//512
    assert len(data)%512==0
    for i in range(count):
        block=data[i*512:(i+1)*512];fields=struct.unpack_from('<8I',block)
        assert fields[:3]==(0x0a324655,0x9e5d5157,0x2000)
        assert fields[3:7]==(address+i*256,256,i,count)
        assert struct.unpack_from('<I',block,508)[0]==0x0ab16f30
        rebuilt+=block[32:288]
    assert rebuilt[:len(payload)]==payload and not any(rebuilt[len(payload):])

def package(board,firmware,out,wifi=None):
    firmware=firmware.resolve().with_suffix('.bin');binary=firmware.read_bytes()
    if not 0<len(binary)<FLASH_OFFSET:raise ValueError('Expected a firmware .bin smaller than 4 MiB')
    if board=='pico' and wifi is not None:raise ValueError('Copy your private wifi.ini onto SD separately for Pico')
    # Use picotool's ELF conversion so boot-family metadata follows the actual build.
    out.mkdir(parents=True,exist_ok=False)
    subprocess.run(['picotool','uf2','convert',str(firmware.with_suffix('.elf')),str(out/'firmware-only.uf2'),'--platform','rp2350'],check=True,capture_output=True)
    verify_uf2((out/'firmware-only.uf2').read_bytes(),binary)
    (out/'apple').mkdir()
    if board=='tufty':
        volume=empty_volume(wifi);(out/'empty-data.img').write_bytes(volume)
        full=bytearray(b'\xff'*FLASH_SIZE);full[:len(binary)]=binary;full[FLASH_OFFSET:]=volume
        # picotool supplies the absolute family when the data spans firmware+storage.
        raw=out/'fresh-install.bin';raw.write_bytes(full)
        subprocess.run(['picotool','uf2','convert',str(raw),str(out/'fresh-install.uf2'),'--family','absolute','--platform','rp2350'],check=True,capture_output=True)
        verify_uf2((out/'fresh-install.uf2').read_bytes(),bytes(full));raw.unlink()
    shutil.copytree(ROOT/'basic',out/'basic',ignore=shutil.ignore_patterns('*.dsk','*.po','*.bdsk','__pycache__'))
    shutil.copytree(ROOT/'docs',out/'docs')
    (out/'README.md').write_text('# Apple II install package\n\n'
        'See [installation](docs/INSTALL.md), [Wi-Fi setup](docs/WIFI-SETUP.md), and '
        '[adding software](docs/DISKS.md). This package contains no application disks or DOS.\n\n'
        '`firmware-only.uf2` preserves your existing disk storage. '
        + ('`fresh-install.uf2` replaces the entire Tufty flash, including saved disks.\n\n' if board=='tufty' else
           'On a fresh Pico SD card, create an empty `apple` folder; keep your existing folder when updating.\n\n')
        + 'Build instructions refer to the source repository. See '
        '[distribution status](docs/DISTRIBUTION.md) before sharing firmware. '
        'Validation is recorded in `manifest.json`; no device is flashed by the packager.\n')
    shutil.copy2(ROOT/'wifi.example.ini',out/'wifi.example.ini')
    for name in ['LICENSE','LICENSE-DOWNSTREAM','THIRD-PARTY-NOTICES.md']:
        shutil.copy2(ROOT/name,out/name)
    shutil.copytree(ROOT/'licenses',out/'licenses')
    # Keep SSH provenance and the original file-level notices with binaries.
    shutil.copytree(ROOT/'third_party/ssh',out/'third_party/ssh')
    files={str(p.relative_to(out)):{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(out.rglob('*')) if p.is_file()}
    (out/'manifest.json').write_text(json.dumps({'board':board,'application_disks':[], 'private_wifi':wifi is not None,'files':files,'validation':'UF2 byte readback; no hardware flashed. Empty data volume contains no Apple applications or DOS.'},indent=2)+'\n')
    print('Created disk-free package: '+str(out))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--board',choices=['pico','tufty'],required=True)
    parser.add_argument('--firmware',type=Path,required=True,help='Matching .elf or .bin build output')
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--wifi',type=Path,help='Optional PRIVATE wifi.ini for a fresh Tufty install')
    args=parser.parse_args()
    package(args.board,args.firmware,args.out,args.wifi.read_bytes() if args.wifi else None)
if __name__=='__main__':main()
