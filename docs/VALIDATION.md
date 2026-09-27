# Validation and current limits

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

Physical USB host keyboard tests on the replacement adapter and Pico 2 W
hardware tests remain pending, as do a true removal-of-power cold start and
physical display/button/audio confirmation. Two USB CDC debug descriptors
became stale during testing; reopening restored monitoring while the web session
remained live. The cause is not established. The console build excludes the
USB host adapter, so this is not a host-keyboard result.

The USB implementation supports one shared keyboard state and standard keyboard
reports; arbitrary NKRO layouts and independent simultaneous keyboards are not
implemented. Host TLS and CI tests do not replace hardware testing. See
[EXTERNAL_MEMORY.md](EXTERNAL_MEMORY.md) for the memory acceptance sequence.

Current limits include 1 KiB custom request headers, 1 KiB request bodies, 4 KiB
responses, one outgoing request at a time and a 30-second deadline. HTTPS uses
TLS 1.2 and a limited bundled root store. Certificate chain and hostname checks
are enabled, but there is no trusted wall clock for certificate date checks.
IPv6, compression, automatic login/token refresh and a JSON parser are not included.

The browser mirrors text, not the full graphics display. Network control is an
explicit per-session feature intended for a trusted local network. Pico display
buttons remain unmapped. Full badge data-image replacement erases its saved disks.

The importer supports 140 KB .dsk/.do/.po, 232960-byte .nib, and the emulator's
35-track .bdsk and compatible 5.25-inch .woz layouts (WOZ up to 1 MiB).
It does not validate whether an application will boot or emulate IIgs hardware.
Uploads use a temporary file and are listed only after validation and close;
existing originals and saved working copies are not overwritten. Sudden power
loss can still damage a FAT filesystem. Keep backups before hardware testing.
