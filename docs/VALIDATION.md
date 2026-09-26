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

**Physical-device validation of these latest networking changes is pending.**
Host TLS tests use the computer's transport, not the RP2350 radio/lwIP stack.
Firmware is provided as source for testing; no newly validated hardware release
is claimed. CI builds do not test physical hardware either.

Current limits include 1 KiB custom request headers, 1 KiB request bodies, 4 KiB
responses, one outgoing request at a time and a 30-second deadline. HTTPS uses
TLS 1.2 and a limited bundled root store. Certificate chain and hostname checks
are enabled, but there is no trusted wall clock for certificate date checks.
IPv6, compression, automatic login/token refresh and a JSON parser are not included.

The browser mirrors text, not the full graphics display. Network control is an
explicit per-session feature intended for a trusted local network. Pico display
buttons remain unmapped. Full badge data-image replacement erases its saved disks.
