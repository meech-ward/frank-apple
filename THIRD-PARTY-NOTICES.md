# Attribution and licenses

This is a downstream of [rh1tech/frank-apple](https://github.com/rh1tech/frank-apple)
by Mikhail Matveev. The original [LICENSE](LICENSE) and source notices are
preserved. Its main license grant is MIT, with third-party components listed
separately. This repository must not be treated as uniformly MIT-licensed.

- **MII emulator**, Michel Pollet: https://github.com/buserror/mii_emu — MIT,
  as identified upstream. Existing source notices remain authoritative.
- **Apple2TS**, Chris Torrence and Michael Morrison: https://github.com/ct6502/apple2ts
  — [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/).
  The 256-byte `mii_rom_smartport` array in `src/mii_smartport.c` is inherited
  unchanged from FRANK Apple/MII. [MII's assembly](https://github.com/buserror/mii_emu/blob/cb27727bb2f8270fcb6545b3514d9f85b99f731c/test/asm/mii_smartport_driver.asm)
  credits its adaptation of Apple2TS's hard-drive ROM and inspiration from
  Terence Boldt's ROM-card driver. This downstream adds a provenance comment;
  it does not change the ROM bytes or relicense them under MIT.
- **MurmDoom / Murmulator drivers**, Mikhail Matveev and contributors: upstream
  lists GPLv2; individual files include `GPL-2.0-or-later` notices, including
  the old USB HID wrapper and PS/2 implementation. Those terms are retained; linking them into firmware
  does not make them MIT. [GPLv2 text](licenses/GPL-2.0.txt). Supported downstream
  Pico/Tufty builds exclude these legacy implementations; other configurations
  need separate review.
- **TinyUSB**, Ha Thach and contributors: https://github.com/hathach/tinyusb —
  [MIT notice](licenses/TinyUSB-MIT.txt). The replacement USB adapter uses its
  HID definitions/ASCII mapping and the existing MIT HID producer.
- **Raspberry Pi PSRAM support**, Raspberry Pi: a pinned, adapted BSD subset
  provides the new Tufty memory driver's timing/register helpers. See
  [source and changes](drivers/vendor/pico_psram/README.md) and the
  [BSD-3-Clause notice](licenses/RaspberryPi-PSRAM-BSD.txt).
- **FatFs**, ChaN: notices and BSD-style terms are embedded in the FatFs sources.
- **SD driver**, Elehobica and Raspberry Pi contributors:
  [driver license](drivers/sdcard/LICENSE) and file-level BSD notices.
- **Pico SDK / pico-extras**, Raspberry Pi: upstream links and file-level licenses
  apply. pico-extras remains a submodule; SDK dependencies are fetched separately.
  SDK component licenses differ; the CYW43 radio driver is not covered by the
  SDK’s general BSD license. See [distribution notes](docs/DISTRIBUTION.md).
- **Pimoroni Tufty 2350**: https://github.com/pimoroni/tufty2350 — portions of the
  parallel display driver and badge definitions are adapted from this project.
  [MIT notice](licenses/Pimoroni-Tufty-MIT.txt). The board header retains its
  Raspberry Pi BSD-3-Clause notice.
- **Pimoroni Pico libraries**: https://github.com/pimoroni/pimoroni-pico — ST7789
  initialization used by the SPI display driver.
  [MIT notice](licenses/Pimoroni-Pico-MIT.txt).

- **Amazon Trust Services roots**: unmodified public CA certificates, CC BY-ND 4.0;
  see [certificate provenance](certs/amazon-roots.md). Other bundled roots retain
  their provider terms; the PEM contents are not a software license grant.

Our original standalone additions (browser lessons, networking helpers,
original BASIC examples, documentation and tools) are available under
[LICENSE-DOWNSTREAM](LICENSE-DOWNSTREAM). Changes to existing files remain
subject to those files' applicable licenses. This does not change any upstream
copyright or third-party license, and does not claim ownership of third-party
logos, Apple ROM data or historical software.

The original Apple ROM arrays already present upstream are retained. They and
application disk images are not relicensed under MIT. No additional historical
application disks, private flash images, or saved user disks are published here.

See [distribution requirements and unresolved issues](docs/DISTRIBUTION.md)
before redistributing a compiled firmware or a preflashed device.
