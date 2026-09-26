# Install firmware and prepare disks

These instructions describe local testing. Read [distribution status](DISTRIBUTION.md)
before sharing built firmware or preflashed devices.

Build the correct `.uf2` using [BUILDING.md](BUILDING.md). Firmware files are
board-specific: a Tufty image is not suitable for the Pico Display Pack build.
These are firmware-only updates, not complete images of your saved disks.

## Pico 2 W

1. With power off, put your SD card in a computer. Use a FAT16/FAT32 card and
   create an `apple` folder at its root.
2. Copy your own Apple II disk images into that folder. Start with a bootable
   DOS 3.3 disk to use Applesoft BASIC and disk saves. The browser guide expects
   names listed in [DISKS.md](DISKS.md), but the disk menu accepts other names.
3. Optionally place `wifi.ini` beside `apple`, following [WIFI-SETUP.md](WIFI-SETUP.md).
4. Eject the card and return it to the Pico. Hold BOOTSEL while connecting a
   USB data cable, then copy the Pico-specific `.uf2` to its bootloader drive.
5. After booting, select and boot your disk from the menu. It is remembered
   for the next startup. With keyboard firmware, connect the powered OTG
   keyboard after flashing.

An optional `/apple/autoboot.txt` contains two lines, for example:

```text
/apple
Workshop.dsk
```

The emulator writes `.bdsk` working files beside the original images. These
contain your changes and take precedence over the originals; preserve them
when backing up or replacing SD files.

## Tufty 2350

Hold BOOT, tap RESET, release BOOT, and copy the Tufty-specific `.uf2` to the
bootloader drive. This updates the first 4 MiB and preserves the data volume.
An existing Apple II volume continues to work. A freshly wiped badge also needs
a 12 MiB FAT16 volume at flash offset 4 MiB; firmware alone does not create it.

On macOS, build a data image from a directory containing your own disk images:

```sh
bash tools/mkfatimg.sh apple-data.img 12 /path/to/my-disks /path/to/wifi.ini
picotool uf2 convert apple-data.img -t bin apple-data.uf2 --offset 0x10400000 --family absolute --platform rp2350
```

The final Wi-Fi argument is optional. The helper places disks in `/apple` and
Wi-Fi settings at `/wifi.ini`. Include `autoboot.txt` in your disk directory if
wanted. Copy `apple-data.uf2` onto the badge's bootloader drive, entering
BOOT + RESET again if the firmware upload already restarted it.

**Replacing this data image replaces existing saved programs.** Back up first.
For an existing badge, prefer a firmware-only update. There is no USB file
manager in this build; changing internal Wi-Fi settings requires updating the
data volume. The SD-card workflow on Pico is simpler.

## BASIC examples and browser guide

Use the browser editor to paste a program from `basic/`, run it, and SAVE it to
your DOS disk. The GET and POST/API buttons populate their complete examples.
The browser's classic-app journeys expect you to supply the corresponding
original disks. This source repository does not distribute those images.
