# FRANK Apple — Tufty and Pico downstream

A downstream fork of **[rh1tech/frank-apple](https://github.com/rh1tech/frank-apple)**,
Mikhail Matveev's Apple IIe emulator for RP2040/RP2350. The original project and
hardware are at [frank.rh1.tech](https://frank.rh1.tech/).

This fork adds support for the **Pimoroni Tufty 2350 badge** and a **Pico 2 W with
Display Pack 2.8 and SD storage**, with an on-device browser Field Guide and
networking from Applesoft BASIC. It preserves upstream history and attribution.
It is an independent downstream, not an official FRANK release.

## What this fork adds

- Tufty parallel LCD, PSRAM, flash-backed disk storage, buttons and activity LEDs.
- A [button launcher](docs/BUTTON-LAUNCHER.md) to browse saved Applesoft programs,
  load/run them without a keyboard, stop BASIC, and select disks.
- Pico 2 W SPI LCD and SD-card support for the wiring described below.
- Browser disk uploads and library, multi-disk Boot/Insert controls, blank data
  disks, and BASIC source-file import. Bring your own applications.
- Local browser typing, a 40/80-column text view, guided Apple II lessons,
  and editable BASIC programs. No account or cloud service required.
- Optional Wi-Fi configured with a separate `wifi.ini` file.
- HTTP/HTTPS GET, POST, PUT, PATCH and DELETE from BASIC, with explicit headers
  and small request bodies. No built-in Supabase project or remote cloud keyboard.
- Storage, keyboard startup, display and emulation fixes, plus host-side tests.
- MIT USB input and MIT/BSD Tufty memory support; supported builds exclude the
  legacy GPL input/memory drivers.

## Start here

1. [Choose your hardware and USB mode](docs/HARDWARE.md).
2. [Build the firmware](docs/BUILDING.md) and [install it](docs/INSTALL.md).
3. Optionally copy [wifi.example.ini](wifi.example.ini) as `wifi.ini` and follow
   [Wi-Fi setup](docs/WIFI-SETUP.md).
4. Open the local Field Guide, or connect a USB keyboard. Try the
   [BASIC examples](basic/) and [HTTP examples](docs/HTTP-GET.md).

The repository contains source, documentation and our BASIC examples. Classic
application disk images, personal disk saves, configured firmware, and local
credentials are not included. Supply your own disk images; firmware-only
flashing does not install DOS or applications. The original upstream ROM data
remains in the source tree and is not relicensed by this fork.

## Distribution status

The supported Pico/Tufty builds now exclude the identified GPL input/memory
drivers and retain Wi-Fi under the SDK's Raspberry-Pi-device license. Inherited
Apple ROMs and the separately licensed SmartPort guest ROM remain as described
in [distribution notes](docs/DISTRIBUTION.md). This driver update is not a blanket
clearance of those embedded assets. No firmware binaries or application disks
are released here.

## Current validation

The hardware ports, disk saves, browser guide and keyboard were exercised on
both boards during development. The latest driver, disk-import, file-based Wi-Fi
and expanded HTTP changes compile in all four board/USB combinations and pass
host tests. **Those latest builds still need physical-device
validation.** See [validation and limits](docs/VALIDATION.md).

## Upstream and licensing

Please credit and link to [FRANK Apple](https://github.com/rh1tech/frank-apple)
when sharing this downstream. Upstream is based on
[MII](https://github.com/buserror/mii_emu) and other credited projects.

The original [LICENSE](LICENSE) is retained unchanged: its main grant is MIT,
and it also lists separately licensed components. Existing file notices and
third-party licenses continue to apply, including the excluded legacy GPL drivers.
[LICENSE-DOWNSTREAM](LICENSE-DOWNSTREAM) covers our original standalone additions;
it does not relicense existing code, Apple ROMs, disk images, or third-party
material. See [third-party notices](THIRD-PARTY-NOTICES.md) for details.

[Upstream's README](docs/UPSTREAM-README.md) remains available for the original
FRANK/Murmulator configurations. Those configurations are not newly validated
by this downstream's board tests.
