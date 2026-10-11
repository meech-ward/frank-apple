# Documentation

Guides for the Tufty 2350 badge and Pico 2 W downstream of FRANK Apple.
[Project overview](../README.md) · [Firmware downloads](https://github.com/meech-ward/frank-apple/releases)

## Start here

1. [Choose your hardware](HARDWARE.md) — supported boards, Pico display/SD wiring,
   and USB keyboard versus console firmware.
2. [Install firmware and prepare disks](INSTALL.md) — choose a download, flash
   the board, and supply the Apple disks it will boot.
3. [Use your Apple II](USING-THE-APPLE-II.md) — find the BASIC prompt, write and
   run a program, save it, and load it again.

Networking is optional. To use your computer as the keyboard, follow
[Wi-Fi setup](WIFI-SETUP.md), then [connect with SSH](SSH.md) or enable the
[browser Field Guide](USING-THE-APPLE-II.md#tufty-badge-controls).

## Everyday use

- [Badge buttons and launcher](BUTTON-LAUNCHER.md) — browse saved BASIC programs,
  run them without a keyboard, stop a program, and choose disks.
- [Add disks and applications](DISKS.md) — upload disk images or BASIC listings,
  handle multi-disk software, create data disks, and back up saves.
- [Wi-Fi and hotspot settings](WIFI-SETUP.md) — edit `wifi.ini`, join your network
  or create a hotspot, and put the settings on either board.
- [Use SSH](SSH.md) — enable login, connect with a normal SSH client, use the
  keyboard shortcuts, and understand what the text mirror shows.
- [HTTP requests from BASIC](HTTP-GET.md) — use GET, POST, PUT, PATCH, and DELETE,
  send headers and bodies, and read responses.
- [BASIC example programs](../basic/) — source listings to try or adapt.
- [Power off and restart](POWER.md) — use RESET, enter the bootloader, and save
  your work before shutting down the Tufty.

## How it works

- [Inside the badge](HOW-IT-WORKS.md) — the emulated Apple IIe, the two RP2350
  cores, RAM and ROM, flash storage, PSRAM floppy caches, and the path of a SAVE.
- [Inside SSH](SSH-INTERNALS.md) — the on-device SSH protocol and encryption,
  host keys, keyboard input, text-screen mirroring, and implementation limits.
- [External memory driver](EXTERNAL_MEMORY.md) — PSRAM initialization, memory
  mapping, bus timing, and the code that must run from internal SRAM.

## Build and share

- [Build from source](BUILDING.md) — toolchain setup, board profiles, and build
  commands for a clean clone.
- [Distribution and licensing](DISTRIBUTION.md) — what the supported builds
  include and what to check before sharing firmware or preflashed devices.
- [Third-party notices](../THIRD-PARTY-NOTICES.md) — component origins, licenses,
  and attribution.
- [Original FRANK Apple README](UPSTREAM-README.md) — upstream's hardware and
  instructions, preserved separately from this downstream's board guides.

## Test status and reports

Start with [Validation and current limits](VALIDATION.md) for what has been
tested and what still needs hardware verification. Each report below records
a particular board and firmware revision:

- [5 October 2026: Tufty SSH and HTTP](HARDWARE-VALIDATION-2026-10-05.md) — physical
  station-mode SSH, shared input, save/load, reconnects, and concurrent HTTP/HTTPS.
- [26 September 2026: Tufty drivers](HARDWARE-VALIDATION-2026-09-26.md) — physical
  console-build checks for memory, flash storage, display, and networking.
