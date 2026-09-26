# Source publication and firmware distribution

This downstream preserves the upstream MIT grant, GPL files and other notices.
It is a source development fork, not a declaration that every resulting firmware
binary is MIT-licensed or cleared for redistribution. No ready-to-flash release
is published here while the issues below remain unresolved.

## GPL code used by the builds

- `drivers/usbhid/usbhid_wrapper.c` and its header explicitly use
  **GPL-2.0-or-later**. The keyboard profiles compile/link that wrapper.
- `drivers/ps2kbd/ps2kbd_mrmltr.cpp`, its header and PIO sources use
  **GPL-2.0-or-later**. CMake builds/links the PS/2 library even when
  `PS2_KEYBOARD_ENABLED=OFF`, because shared turbo/display state lives in its
  wrapper. Link maps include the archive; some routines are discarded. The
  OFF setting alone is not proof of a GPL-free executable.
- Upstream's LICENSE also attributes HDMI, PSRAM, PS/2 and USB HID drivers to
  MurmDoom under GPLv2. Tufty uses PSRAM. Several driver files have no precise
  individual grant, so a permissive-only claim needs a provenance audit.

The original GPL driver files above are unchanged from upstream main. The
[GPLv2 text](../licenses/GPL-2.0.txt) is included. Existing licenses on individual
MIT/BSD files remain available; a combined program containing GPL code must also
satisfy the GPL's requirements for the combined work.

For a firmware download, credits alone are insufficient. Provide the applicable
GPL terms and notices, and complete corresponding source matching the binary,
including build/install scripts and necessary dependency source. A complete
source archive next to each firmware download is the intended release approach;
GitHub's automatic archive omits submodule contents and is not by itself a full
dependency bundle. See [GPLv2 sections 2–3](https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
Shipping preflashed physical badges needs its own compliant source-delivery
arrangement; a URL alone is not the GPLv2 written-offer option.

## Wi-Fi compatibility issue still open

Pico SDK 2.2.0 pins CYW43 driver commit
`dd7568229f3bf7a37737b9e1ef250c26efe75b23`. Its default license permits only
non-commercial use; its alternate [LICENSE.RP](../licenses/CYW43-LICENSE.RP.txt)
permits use and redistribution only with Raspberry Pi semiconductor devices.
RP2350 meets the hardware condition, but the restriction on recipients is a
separate compatibility concern when linking that driver into a GPL program.
Neither publishing our source nor adding attribution removes that restriction.
No additional GPL linking permission has been established for this downstream.

Before distributing a combined Wi-Fi/GPL firmware, resolve this through an
applicable permission/exception from the relevant rights holders, or a verified
implementation using compatible components. Do not assume that disabling USB
keyboard or PS/2 alone resolves all driver licensing. No rights-holder outreach
has been made as part of preparing this repository.

Mbed TLS at the SDK's pinned revision offers Apache-2.0 **or** GPL-2.0-or-later;
the GPL option is available. Its Apache option alone would not be compatible
with GPLv2-only code. Dependency notices must accompany any eventual release.

## Apple ROMs and application disks

The Apple ROM C arrays were already present upstream and remain in this fork.
Their presence upstream is not evidence of a separate redistribution grant.
Their rights and any compatibility implications must also be resolved before
claiming a fully cleared firmware package. This fork does not relicense them.
Classic application disks and personal data images are not included. Supplying
an image in an installer requires permission for that image independently of
the emulator's source-code licenses.

The current source fork preserves provenance and exposes the implementation;
these notes are not a blanket legal clearance for every file or resulting build.
