# Source publication and firmware distribution

This downstream preserves each component's license. The repository is not
uniformly MIT-licensed. The supported Pico 2 W and Tufty profiles now use the
replacement drivers below; historical drivers remain in the source tree for
legacy configurations.

## Supported downstream builds

`tools/build-public-firmware.py` builds both boards with console or USB keyboard
input. These four profiles use:

- **USB keyboard:** original MIT `drivers/usbhid/apple_usb_input.c`, the existing
  MIT HID report producer, and TinyUSB under its MIT license.
- **Tufty external memory:** original MIT `drivers/board_memory.c`, with a pinned
  BSD-3-Clause subset of Raspberry Pi's PSRAM support. There is no allocator:
  the emulator uses fixed disk-cache regions. See [implementation and source
  provenance](EXTERNAL_MEMORY.md).
- **Shared input controls:** original MIT `src/input_controls.c`, independent of
  PS/2. PS/2 is disabled and its library is neither built nor linked.

The build tool checks the active compilation sources, header dependencies and
link map to exclude the old USB wrapper, PS/2 library, and PSRAM init/allocator.
The unused NES pad implementation is also excluded from these two boards.
This is evidence about these build profiles, not every possible configuration
or every embedded asset. Physical testing of the replacement drivers is tracked
in [VALIDATION.md](VALIDATION.md).

Keep the [TinyUSB MIT notice](../licenses/TinyUSB-MIT.txt),
[Raspberry Pi PSRAM BSD notice](../licenses/RaspberryPi-PSRAM-BSD.txt), and all
other applicable notices with distributions. The package tool includes them.

## Wi-Fi and TLS

Pico SDK 2.2.0 pins CYW43 commit
`dd7568229f3bf7a37737b9e1ef250c26efe75b23`. These RP2350 builds use its alternate
[Raspberry Pi license](../licenses/CYW43-LICENSE.RP.txt), which permits use and
redistribution with Raspberry Pi semiconductor devices. Retain that license and
its notices; it does not give permission for arbitrary non-Raspberry-Pi hardware.
Mbed TLS is used under its Apache-2.0 option.

The SSH feature adds a pinned BSD-3-Clause staticnet subset, public-domain
TweetNaCl/libb64 code, and original MIT adapters. It reuses the existing
Apache-2.0 mbedTLS backend and does not introduce wolfSSH or pico-sshd. Retain
the [SSH provenance](../third_party/ssh/README.md),
[staticnet notice](../licenses/staticnet-BSD-3-Clause.txt), and
[Mbed TLS license text](../licenses/MbedTLS-LICENSE.txt) with its distributions.

Removing the identified GPL driver implementations from the four supported
profiles removes that specific GPL/CYW43 linking issue. It does not relicense
the retained legacy source or resolve the separate embedded-ROM questions below.

## Legacy configurations

The old USB wrapper, PS/2 implementation, and other MurmDoom-derived drivers
remain with their original notices. Upstream attributes HDMI, PSRAM, PS/2 and
USB HID drivers to MurmDoom under GPLv2; several files explicitly specify
GPL-2.0-or-later. Our small PS/2 wrapper change moves shared state into the new
input module; it does not change the wrapper's license.

Enabling legacy PS/2 or selecting other display/board drivers needs a separate
build and license review. Do not combine GPL code with the RP-restricted CYW43
driver on the assumption that attribution alone resolves the restrictions.
No linking exception or rights-holder permission has been obtained.
Distributing a GPL combined work also needs complete corresponding source and
the other applicable obligations in [GPLv2](../licenses/GPL-2.0.txt).

## Embedded ROMs and application disks

The Apple ROM arrays already present in FRANK Apple remain unchanged. Their
presence upstream is not a separate redistribution grant; this driver replacement
does not settle those rights or relicense the arrays.

There is also a distinct **256-byte SmartPort guest ROM** in
`src/mii_smartport.c`. MII's assembly identifies it as adapted from Apple2TS,
whose license is CC BY-SA 4.0. It remains unchanged here. Keep the authors,
source/adaptation history and license links in [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md).
This is an actual embedded component, not merely a reference to Apple2TS.

Classic application disks, DOS disks, and personal data images are not bundled.
Users add their own images through the browser or SD card, as described in
[DISKS.md](DISKS.md). The package tool creates an empty data volume.

These driver changes are not a blanket clearance of inherited ROM assets or
every possible firmware combination. No ready-to-flash public release is
published by the source-check workflow.
