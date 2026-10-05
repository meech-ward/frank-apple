# Validation and current limits

## SSH and portable hotspot — 5 October 2026

The new SSH/hotspot firmware builds for all four Tufty/Pico keyboard/console
profiles. It has **not yet been exercised on a physical board**. There was no
connected badge available for this change; the earlier hardware results below
do not establish that the new radio mode or SSH timing works on hardware.

Software validation includes:

- The actual embedded SSH engine and the SDK's mbedTLS sources, compiled on the
  host under ASan/UBSan, communicating with stock OpenSSH using its normal
  cryptographic defaults. Password login, PTY/shell, keyboard bytes, a 64 KiB
  paste with a deliberately slow consumer, a rejected second connection,
  reconnects, wrong passwords, EOF draining, and corrupted encrypted data are
  checked. Rekey requests disconnect cleanly with a reconnect message.
- Malformed framing/parser tests, bounded channel windows, interrupt priority,
  and authentication-state checks. These are regression tests, not a claim of
  an independent cryptographic security audit.
- The production raw TCP adapter with the SDK's real pbuf allocation/free code:
  chained packets, receive backpressure, oversized input, connection errors,
  close failures, and reconnect ordering. Sanitizers check buffer ownership.
  The review regression combines the actual SSH parser with that adapter: an
  almost-complete maximum-size packet followed by its tail and the next packet
  must keep advancing. Prefix consumption fixes the former whole-chain stall.
  A connect/FIN before the first poll also releases the single session slot.
- ANSI screen tests that apply the output to a terminal model and compare cells:
  inverse text, 40/80-column changes, small windows, fragmented key sequences,
  input/output backpressure, and reconnects. The adapter leaves the host in its
  ordinary terminal buffer with a visible cursor.
- Real control-layer tests for configuration, storage failure, manual on/off,
  connection isolation, pasted input, Ctrl-C, EOF, and bounded cleanup.
- Host-key creation, durable readback, corruption detection, and recovery from
  an interrupted first write. Existing invalid identities are not regenerated.
- Station/hotspot configuration, DHCP lease and malformed-packet tests, and
  SDK/lwIP adapter tests for AP-only packet routing and failure cleanup.
- The config updater edits an image through real FatFs and verifies every
  unrelated file, including saved disks and the host key. Its USB orchestration
  is tested with a fake picotool, including interrupted writes and retained
  backups; physical USB updating is still pending.
- The existing disk, emulator, browser-guide, USB-input, power, launcher, HTTP,
  and package checks still pass. The launcher renderer was inspected with its
  SSH row and connection command.
- SmartPort block-storage tests inject sync, seek, write, and short-write
  failures into the production adapter and check two-drive round-trips.
  A failed FatFs sync now returns an error to the emulated machine instead of
  reporting a successful write. The floppy path already checked sync errors.

The linker reserves a 12 KiB core-0 stack in main SRAM, separate from core 1's
2 KiB video stack. ARM compiler stack-usage reports put the conservative SSH
signing call chain around 5.1 KiB before its small callers/interrupt overhead.
All four builds retain more than 90 KiB of static heap headroom. This is not a
measurement of live heap/stack peaks during simultaneous HTTPS and SSH.

Physical acceptance still needed: connect from macOS/Linux on both an existing
Wi-Fi network and the badge hotspot; type/paste, stop with Ctrl-C, save/reload,
disconnect/reconnect, run browser/HTTPS alongside SSH, and verify USB keyboard
and RESET power behavior remain normal. The firmware-only update preserves
the existing data volume. See [SSH usage](SSH.md) and [Wi-Fi setup](WIFI-SETUP.md).

## Previous hardware and software checks

**Tufty power-off addition:** all four firmware profiles build, and the
production-driver host tests pass for short/long RESET, boot reasons, LED
feedback, timer wrap, peripheral parking and power-transition failures.
The Tufty keyboard image was then flashed and byte-verified, preserving disk
storage. The user confirmed that holding RESET turns it off and a short press
turns it back on. Battery current is not measured. The broader hardware tests
below predate this addition. See [POWER.md](POWER.md).

Earlier revisions of both ports were exercised on physical boards: DOS boot,
disk saves across resets, browser controls, VisiCalc, AppleWorks and keyboard
input. The first-command UART break bug was fixed and physical PRINT 2+2 was
confirmed on the Pico keyboard build.

The latest file-based Wi-Fi and expanded HTTP changes have passed:

- Builds for Tufty/Pico, each with console/USB keyboard variants.
- Sanitizer tests of production HTTP serialization/parsing and network-card state.
- Wi-Fi file parsing and FatFs-loading/error tests.
- Browser guide, CPU bus, and mixed graphics/text tests.
- Live HTTPS echo tests on a host for GET, POST, PUT, PATCH and DELETE.

The disk-library addition also passes all four firmware builds and:

- Production FatFs tests with complete upload/readback, arbitrary chunk boundaries,
  cancellation/expiry, duplicate names and existing saved sidecars, malformed WOZ,
  pagination, full volumes and injected write/sync failures (ASan/UBSan).
- Production HTTP-handler tests of fragmented binary requests, size limits,
  the same-origin control header and deferred mount requests (ASan/UBSan).
- Browser tests of upload progress, cancellation, filename checks, BASIC source
  review, and explicit Boot/Insert actions. A real browser with a local mock API
  was also checked at its narrow default viewport: file selection, upload,
  disk listing, boot confirmation/cancel and blank-disk instructions.
- Generated empty FAT16 volumes mounted and written by the real FatFs library.
  Packaging verifies the complete UF2 payload against its firmware/data inputs.

The permissive driver replacement also passes all four firmware builds and:

- USB tests using production callbacks and TinyUSB definitions: BASIC text,
  shifted punctuation, control keys, Return, navigation/menu, speed/turbo,
  keypad directions, Apple buttons, reset, generic reports and gamepad mapping.
  Modifier snapshots, stable releases, unplug cleanup and queue-overflow recovery
  are checked with ASan/UBSan.
- PSRAM timing and capacity tests with ASan/UBSan, including over 2,000 valid
  clock combinations and invalid/unknown chip IDs. The 252/84 MHz settings match
  the pinned Raspberry Pi timing implementation.
- Active compilation-source/header and final link-map checks: the retired
  USB wrapper, PS/2 library and PSRAM init/allocator are absent in all four
  supported profiles. CI/build scripts enforce those exclusions.
- Tufty disassembly review: the direct-mode memory commands and their called
  functions/literal pools execute from SRAM while flash access is unavailable.

**Tufty console hardware validation completed on 26 September 2026:** the
8 MiB six-pass memory diagnostic, dirty-cache preservation across flash writes,
DOS saves, byte-for-byte upload readback, VisiCalc calculation/save/load,
AppleWorks 80-column editing and shortcuts, and live HTTP GET/POST/PUT/PATCH/DELETE
plus error cases passed. The normal firmware was then flashed and verified;
a full board reboot restored a saved BASIC program and HTTPS worked again.
An empty-string bug found in the BASIC HTTP examples was fixed.
See [the hardware report](HARDWARE-VALIDATION-2026-09-26.md) for exact scope.

The Tufty keyboard profile was subsequently flashed and verified. The user
confirmed physical `PRINT 2+2` plus Return worked, and separately confirmed a
normal display and a program running through the web UI. Extended physical
keyboard repeat/modifier/unplug/gamepad checks, audio/button-matrix checks and
Pico 2 W hardware testing remain outside this completed smoke test.
A user power-cycle/reconnect also restored a saved test program and Wi-Fi;
see the hardware report for the exact sequence and startup observations.
Two USB CDC debug descriptors became stale during console testing; reopening
restored monitoring while the web session remained live. The cause is not
established, and the console build excludes the USB host adapter.

The USB implementation supports one shared keyboard state and standard keyboard
reports; arbitrary NKRO layouts and independent simultaneous keyboards are not
implemented. Host TLS and CI tests do not replace hardware testing. See
[EXTERNAL_MEMORY.md](EXTERNAL_MEMORY.md) for the memory acceptance sequence.

Current limits include 1 KiB custom request headers, 1 KiB request bodies, 4 KiB
responses, one outgoing request at a time and a 30-second deadline. HTTPS uses
TLS 1.2 and a limited bundled root store. Certificate chain and hostname checks
are enabled, but there is no trusted wall clock for certificate date checks.
IPv6, compression, automatic login/token refresh and a JSON parser are not included.

The browser mirrors text, not the full graphics display. Browser control is an
explicit per-session feature; SSH startup is controlled by `wifi.ini`. Pico display
buttons remain unmapped. Full badge data-image replacement erases its saved disks.

The importer supports 140 KB .dsk/.do/.po, 232960-byte .nib, and the emulator's
35-track .bdsk and compatible 5.25-inch .woz layouts (WOZ up to 1 MiB).
It does not validate whether an application will boot or emulate IIgs hardware.
Uploads use a temporary file and are listed only after validation and close;
existing originals and saved working copies are not overwritten. Sudden power
loss can still damage a FAT filesystem. Keep backups before hardware testing.
# Button launcher update — 2026-10-01

The button-launcher update passes all four firmware builds (Tufty/Pico,
keyboard/console), the active-driver license checks, 13 Python host checks,
and the browser Field Guide checks. The new catalog and menu checks run with
ASan/UBSan. A private, previously saved System Master BDSK was also read
successfully: 14 Applesoft programs, including user-created files. This exercised
real unaligned writes in addition to synthetic rotated/corrupt tracks.

Production-renderer screen images were inspected for the HOME menu, program
list, Run/Load actions, unavailable BASIC prompt and unreadable catalog.
The menu harness checks unchanged Apple RAM after closing, blocked commands
after snapshot failures, Back/wrap navigation, stop, web toggle and disk access.

The user subsequently confirmed browsing the saved-program catalog and running
a program using the badge buttons. The September 26 keyboard and power tests
above describe that earlier build. See [button controls and limits](BUTTON-LAUNCHER.md).

## Button arrangement follow-up — 2026-10-01

The revised arrangement uses A for Back and B/C for Select throughout the menus.
Web control and disk Read-only are selectable rows. Outside the menus, A sends
Escape, B Return and C Right. The production menu harness exercises the actual
button mapping through catalog Run/Load, cancel/back, web toggles and read-only
changes (including ensuring that toggling a setting does not mount a disk).
Menu rendering and the browser guide are checked too.

**Physical validation of this revised arrangement is pending.** The user's
successful catalog/launch test used the previous button mapping.
