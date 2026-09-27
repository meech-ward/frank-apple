# Badge driver validation — 26 September 2026

**Normal console firmware flashed and checked.** This report records the connected
Tufty badge's completed checks and the physical tests still outstanding. It does
not carry forward physical passes from earlier firmware revisions.

## Configuration and scope

The flashed diagnostic firmware is based on commit `d104bb5`: Tufty console
profile, 252 MHz CPU, 84 MHz PSRAM target, 8 MiB external memory, Wi-Fi enabled.
Firmware loading and verification completed, followed by DOS boot and a working
network connection. The console profile uses USB CDC; it excludes the replacement
USB host keyboard adapter. These device results therefore validate the memory,
storage and network paths, not physical USB host keyboard behavior.

The raw session evidence is under `logs/badge-driver-validation/`. Early
`preflight.json` records the state before identity confirmation, backup and
flashing; its initial `flashed: false` is not the final session state.
Network addresses, service URLs and credentials are intentionally omitted here.

## Completed physical checks

| Check | Recorded result | Evidence file(s) |
| --- | --- | --- |
| Diagnostic firmware flash | Loading and flash verification reached completion; diagnostic boot followed. | `flash-diagnostic.log`, `diagnostic-boot.log` |
| Full external-memory diagnostic | All 8,388,608 bytes exercised over six passes: 12,582,912 32-bit checks, zero errors, reported duration 3,345 ms. | `diagnostic-boot.log` |
| PSRAM preservation across DOS writes | A 4 KiB sentinel was armed at offset `0x007ff000`; DOS track writes and motor-off occurred; subsequent check passed, 1,024 words and zero errors. | `live-console-2.log` |
| PSRAM preservation across library uploads | Second sentinel arm followed by the upload exercise and another 1,024-word, zero-error pass. | `live-console-2.log`, `library-qa.json`, `live-console-3.log` |
| DOS SAVE and LOAD/RUN in the current session | `DRVQA926` saved, then loaded and ran, displaying `DRIVER CHECK PASSED`. It also survived booting VisiCalc, AppleWorks and returning to Workshop. The normal-firmware reboot check below also passed. | `dos-save.json`, `dos-load.json` |
| Web controller | HTTP input, fragmented request handling, rejection limits, multiline pacing, menu, Stop and graphics-state checks passed. | `web-control.log` |
| Live disk library | QA script reports PASS after 479 requests. Two unique 143,360-byte files were added: an original sector-pattern fixture and the same zero-filled blank-image format created by the browser. Chunk progress, cancellation, malformed WOZ rejection, duplicate rejection, listing and unchanged pre-existing file sizes/mount state were checked. No insert or boot was requested by this run. | `library-qa.log`, `library-qa.json` |
| Live HTTP API after example correction | GET, POST, PUT, PATCH and DELETE each returned HTTP 200, network error 0 and the expected echoed marker. | `http-api-fixed.log` |
| Live HTTP edge cases | All 14 checks passed: query parameters, plain HTTP, redirect, chunked response, HTTP 404, HTTP 204, redirect limit, oversized response, invalid URL, invalid certificate, DNS failure, deadline expiry, Ctrl-C cancellation and raw binary response. The final two share one PASS line in the log. | `http-edges.log` |

Additional completed application/storage checks:

- VisiCalc calculated 6 × 7 = 42, then recalculated 8 × 7 = 56. A sheet saved
  as `DRVQA926` on its Drive 2 data disk restored 56 after an unsaved change to 63.
- AppleWorks booted from Startup, accepted the Program disk without restarting,
  opened an 80-column document, preserved mixed case and punctuation, and
  opened/closed its original Help using Open Apple. This test document was not saved.
- Returning to Workshop restored the saved `DRVQA926` BASIC program and result 42.
- Full 12 MiB data-volume readback matched SHA-256 for both uploaded QA files.
  Cancellation/invalid-image fixtures were absent. Only the two named QA files
  were removed from a copy; all 14 other Apple-directory files remained byte-identical.
  Evidence: `classics.log`, application screen JSON files, and `disk-readback.json`.

The full-memory diagnostic and the two sentinel checks are complementary: the
first checks the complete address range at startup; the latter check a reserved
4 KiB region around real flash-writing activity. Neither establishes a
long-duration memory or radio soak. The library protocol checks were followed by direct flash readback, which verified
the uploaded payload bytes independently of the web API.

## Defects and observations during this session

The original HTTP BASIC example failed with `ILLEGAL QUANTITY ERROR IN 9520`
when it attempted to process an empty string. Commit `dd591cf4` adds the
empty-string guard to `basic/http-api-demo.bas` and `basic/http-demo.bas`.
The fixed API example subsequently passed all five live methods listed above.
The empty-URL guard in the separate GET sample was inspected but not separately
exercised as an empty-URL BASIC program in this session.
This was an Applesoft example defect, not evidence that a network request had
failed. `http-api.log` preserves the initial failure; `http-api-fixed.log`
records the successful rerun.

The edge-case results include expected error handling: invalid certificates,
DNS failures, deadlines and size/redirect limits passed by producing the
expected rejection or error outcome, rather than by accepting the request as
successful. The application outcomes are recorded above.

Two stale USB serial file descriptors produced `ENXIO` during the session while
the web session remained live. The cause has not been established. This
observation is not classified as a badge reset, a PSRAM failure, or a USB host
keyboard result. Reopening the console and continuing tests is not itself a
root-cause diagnosis.

The USB flash helper's BIN argument order was corrected, and it now propagates
load failures rather than hiding them behind output filtering and proceeding
to reboot. Its argument order and failure handling were checked using a mocked
tool without further hardware access.

## Normal builds and packaging

`diagnostics-off.json` records that all four normal profiles were byte-identical
with diagnostics disabled and contained no linked diagnostic code at that
comparison point. Current normal packages contain the corrected BASIC samples. The normal Tufty
console ELF was subsequently loaded with flash verification and booted with
diagnostics absent. It mounted Workshop, joined Wi-Fi and reached BASIC.
After `NEW`, `LOAD DRVQA926` and `RUN` restored the saved message and result 42.
A public HTTPS GET returned status 200, error 0, with the cloud keyboard disabled.
Evidence: `flash-normal.log`, `normal-boot.log`, `normal-check.log`,
`normal-persistence.json`, `normal-network.json` and `normal-library.json`.
The five-method and edge-case suite above ran on the diagnostic build; this last
normal-build check is a boot, persistence and HTTPS smoke test.

The two library QA images were verified and removed in the readback copy.
The cleaned volume prefix was restored and flash-verified; the final library
listing confirms those temporary files are absent, and the saved program still runs.
The small DOS and VisiCalc files named `DRVQA926` remain as reproducible test results.

## User power-cycle follow-up

After being asked to remove USB/battery power, wait five seconds and reconnect,
the user reported “plugged”. The capture recorded disconnection/reconnection and
a full startup, PSRAM initialization and Workshop autoboot. After BASIC was ready,
`NEW`, `LOAD POWERQA926`, `HOME`, `RUN` restored `POWER CHECK PASSED` and `56`.
This verifies the saved program after the requested user power-cycle sequence;
the earlier normal-firmware check used a software reboot.

Wi-Fi reconnected. A later captured startup needed automatic join retries before
connecting; no credentials or firmware changes were required. One serial load
attempt was interrupted by a USB disconnect during a fresh startup; it was
repeated successfully after BASIC was ready. Cause of that additional restart
has not been established. Evidence: `power-cycle-console.log`,
`power-cycle-events.jsonl`, `power-cycle-load.log`, `power-test-status.json`.

Web control starts off after reset. The user has been asked to press HOME, C,
HOME to test physical buttons and re-enable it, and to confirm the screen and
powered USB-C keyboard setup. Those responses are still pending. Removal of
power while a disk write is active has not been tested.

## Remaining acceptance checks
- Finish any planned display, audio and physical-button interactions; no new
  pass for those controls is inferred from HTTP or console success.
- Test USB host keyboard typing, repeat, modifiers, unplug and gamepad behavior
  on a keyboard profile with the corresponding physical peripherals. Console
  testing cannot substitute for that check.
- Physical Pico 2 W profile testing remains separate from this Tufty session.

The user subsequently confirmed that the screen looked normal and a program ran
through the web UI. The badge's keyboard-host firmware has now been flashed with
picotool verification; physical keyboard typing is still awaiting the user.
Its prepared ELF SHA-256 matches the loaded image, and the firmware-only write
preserves Wi-Fi and disk data. The program previously in RAM was saved separately.
The first load encountered a communication error; the retry verified completely.
Post-write USB visibility disappeared before a separate info/reboot command could
run, so the user was instructed to tap RESET after connecting the powered keyboard.
Evidence: `flash-keyboard-retry.log`, `keyboard-image-info.txt`,
`keyboard-test-status.json`. HOME is the BOOT button for future recovery.
The original full-device backup and final data-volume readback are retained
locally with restricted file permissions. No GitHub push or release was performed.

These remaining items need their own concrete evidence.
The report is a bounded record of observed tests, not a claim that every feature
or every hardware configuration has passed.
