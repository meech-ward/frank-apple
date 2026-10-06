# Tufty SSH hardware checks — 5 October 2026

**Station-mode SSH works on the physical Tufty**, including shared browser input
and outgoing HTTPS. These results apply to the Tufty keyboard firmware in
[the reviewed SSH release](https://github.com/meech-ward/frank-apple/releases/tag/apple2-ssh-hotspot-2026-10-05-r2).
They do not establish hotspot or Pico 2 W behavior.

## Tested setup

- Tufty 2350, RP2350 A4, 16 MiB flash, 8 MiB PSRAM; USB keyboard firmware profile.
- Source `abc0c0009d1a8bfaff4119a8196550120fabf0f8`.
- `tufty-apple2-keyboard-firmware-only.uf2` SHA-256:
  `dc8c20b0312e42cd542e0353962edeea1d922907ee46afb4ae08d1a23c930e8f`.
- Firmware and disk volume written and verified against flash before rebooting.
- WPA2 station connection; stock macOS OpenSSH with password authentication and
  its normal algorithm negotiation. Later connections checked the same host key.
- Keyboard input and screen observations came through SSH and the browser API.
  A physical USB keyboard was not operated during these checks.

The original program in memory was saved before testing. Tests used separate
temporary BASIC files and synthetic HTTP bodies, without changing existing
program files. Network credentials and host-key material are not included here.

## Passed checks

| Area | Observed result |
| --- | --- |
| SSH input | Return, Backspace, arrow/menu keys, Ctrl-C, and bracketed CRLF paste worked. An 83-line, 993-byte pasted program computed the expected sum of 3,240. |
| Shared session | Browser typing appeared through SSH and SSH typing appeared in the browser. A second SSH client was rejected while the first remained usable. |
| Disk saves | SAVE → NEW → LOAD → RUN returned the same result. The saved program also survived an emulated Apple cold boot and disk remount. |
| Launcher | The saved-program catalog found and ran the test program. Escape backed through the menus and resumed BASIC. |
| Display | Both 40- and 80-column text matched the Apple screen. A 66-character line remained on one line in 80-column mode; returning to 40 columns worked. |
| Session lifecycle | EOF/disconnect preserved Apple state. Reconnects, a 95-second idle connection, wrong-password rejection, and 12 connections closed before handshake all recovered normally. |
| HTTP methods | GET, POST, PUT, PATCH and DELETE reached a local echo service with the expected method, header and body. |
| HTTP responses | Relative redirects, chunked bodies, empty 404 responses, oversized-body rejection, delayed responses, and Ctrl-C cancellation behaved as documented. A request after cancellation succeeded. |
| HTTPS coexistence | A verified HTTPS GET to `https://httpbin.org/uuid` returned 200 while SSH and browser polling remained active. |
| Browser polling | All 212 requests in the concurrent network suite completed successfully; the slowest observed response was 2.20 seconds. This is an observation, not a latency guarantee. |
| Error recovery | Invalid URLs, connection refusal, malformed HTTP, redirect loops, untrusted TLS certificates and the request deadline returned their documented errors. A normal request then succeeded. |
| Upload safeguards | An existing disk name was rejected. Cancelling a partial upload left the disk library unchanged. Browser mutation without the control header was rejected. |
| SSH toggle | Turning SSH off from its menu closed the client. Re-enabling it through the browser restored login with the same host key. |

No firmware changes were needed during this hardware pass.

## Not established by these tests

- Hotspot association, DHCP, and hotspot SSH. The keyboard build has no remote
  bootloader/configuration command; changing modes needs physical BOOTSEL access
  or a connected Debug Probe.
- The USB configuration updater's complete backup/write/readback workflow.
- A physical power cycle after saving, RESET power-off, physical buttons, USB
  keyboard behavior, or unplug/replug on this firmware revision.
- Pico 2 W hardware, a Linux client connected to hardware, or long-duration soak
  testing. Linux OpenSSH is covered by the separate host interoperability suite.
- Measured live heap/stack high-water or a repeat of the full PSRAM diagnostic.

Raw screen captures, client logs, local-server request receipts, and assertions
are retained in the private validation workspace under
`logs/tufty-hardware-qa-2026-10-05/`. See [validation and limits](VALIDATION.md),
[SSH setup](SSH.md), and [Wi-Fi/hotspot setup](WIFI-SETUP.md).
