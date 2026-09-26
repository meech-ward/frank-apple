#!/usr/bin/env python3
"""Build credential-free firmware for the supported Pico and Tufty hardware.

Set PICO_SDK_PATH and put an Arm GCC toolchain on PATH (or set
PICO_TOOLCHAIN_PATH). Does not connect to or flash hardware.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--board',choices=['pico','tufty','all'],default='all')
parser.add_argument('--usb',choices=['console','keyboard','all'],default='all')
parser.add_argument('--jobs',type=int,default=6)
args=parser.parse_args()
env=os.environ.copy()
if not env.get('PICO_SDK_PATH'):
 parser.error('Set PICO_SDK_PATH to a Pico SDK 2.2.0 checkout with submodules.')
if args.jobs < 1:
 parser.error('--jobs must be positive')
for board in (['pico','tufty'] if args.board=='all' else [args.board]):
 for usb in (['console','keyboard'] if args.usb=='all' else [args.usb]):
  kind='p2w' if board=='pico' else 'tufty'
  build=root/('build-public-'+kind+('-kbd' if usb=='keyboard' else ''))
  options=['-DBOARD_VARIANT='+('P2W' if board=='pico' else 'TUFTY'),'-DPICO_BOARD='+('pico2_w' if board=='pico' else 'pimoroni_tufty2350'),'-DVIDEO_TYPE='+('SPI' if board=='pico' else 'PAR'),'-DCPU_SPEED=252','-DAUDIO_TYPE=PWM','-DPS2_KEYBOARD_ENABLED=OFF','-DUSB_HID_ENABLED='+('ON' if usb=='keyboard' else 'OFF'),'-DNETCARD_ENABLED=ON','-DNETCARD_TLS=ON','-DNETCARD_TLS_VERIFY=ON','-DNETCARD_WEB_CONTROL=ON','-DDEBUG_LOGS_ENABLED=ON','-DCMAKE_BUILD_TYPE=Release']
  if board=='tufty':options+=['-DPSRAM_SPEED=84']
  subprocess.run(['cmake','-S',str(root),'-B',str(build),'-G','Ninja',*options],check=True,env=env)
  subprocess.run(['ninja','-C',str(build),'-j',str(args.jobs)],check=True,env=env)
  subprocess.run([sys.executable,str(root/'tools/check-driver-build.py'),str(build)],check=True,env=env)
