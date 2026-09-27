# Install firmware and prepare disks

These instructions describe local testing. Read [distribution status](DISTRIBUTION.md)
before sharing built firmware or preflashed devices.

Build the correct `.uf2` using [BUILDING.md](BUILDING.md). Firmware files are
board-specific: a Tufty image is not suitable for the Pico Display Pack build.
These are firmware-only updates, not complete images of your saved disks.

## Pico 2 W

1. With power off, put your SD card in a computer. Use a FAT16/FAT32 card and
   create an `apple` folder at its root.
2. Leave `apple` empty, or copy your own disk images into it. You can add disks
   later in the browser using **Add software**. A bootable DOS disk adds CATALOG,
   SAVE and LOAD; ROM BASIC itself does not require a disk.
3. Optionally place `wifi.ini` beside `apple`, following [WIFI-SETUP.md](WIFI-SETUP.md).
4. Eject the card and return it to the Pico. Hold BOOTSEL while connecting a
   USB data cable, then copy the Pico-specific `.uf2` to its bootloader drive.
5. Open Web control and add software, or select a disk already on the card. A disk you boot is remembered
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

To turn it off, save your work and wait for disk activity to finish, then hold
**RESET for about two seconds**. Release it when the rear LEDs go out. A short
RESET press turns it on again. See [power controls and save behavior](POWER.md).

Hold HOME (the badge’s BOOT button), tap RESET, release HOME, and copy the Tufty-specific `.uf2` to the
bootloader drive. This updates the first 4 MiB and preserves the data volume.
An existing Apple II volume continues to work. A freshly wiped badge also needs
a 12 MiB FAT16 volume at flash offset 4 MiB; firmware alone does not create it.

For a fresh local install with an empty data volume, use:

```sh
python3 tools/package-clean.py --board tufty --firmware build-public-tufty/tufty-frank_apple-PAR-252MHz-P84-PWM-RC24.elf --out /path/to/new-package --wifi /path/to/private/wifi.ini
```

The Wi-Fi argument is optional. Copy `fresh-install.uf2` to the bootloader drive.
**This is a fresh install: it replaces existing disk storage.** It bundles no
application disks or DOS. Open Web control and use **Add software** afterwards.
The `firmware-only.uf2` in the same package preserves existing disks.

A Pico package uses `--board pico` with its matching firmware and has an empty
`apple` folder for the SD card. Put your private `wifi.ini` beside that folder.

As an alternative on macOS, build a data image from an empty directory (or a
private directory of your own disk images):

```sh
bash tools/mkfatimg.sh apple-data.img 12 /path/to/my-disks /path/to/wifi.ini
picotool uf2 convert apple-data.img -t bin apple-data.uf2 --offset 0x10400000 --family absolute --platform rp2350
```

The final Wi-Fi argument is optional. The helper places disks in `/apple` and
Wi-Fi settings at `/wifi.ini`. Include `autoboot.txt` in your disk directory if
wanted. Copy `apple-data.uf2` onto the badge's bootloader drive, entering
HOME + RESET again if the firmware upload already restarted it.

**Replacing this data image replaces existing saved programs.** Back up first.
For an existing badge, prefer a firmware-only update. The browser library adds disks without replacing the volume. There is no USB
file manager; changing internal Wi-Fi settings still requires updating the
data volume.

## BASIC examples and browser guide

Use the browser editor to paste a program from `basic/`, run it, and SAVE it to
your DOS disk. The GET and POST/API buttons populate their complete examples.
The browser's classic-app journeys expect you to supply the corresponding
original disks. This source repository does not distribute those images.
