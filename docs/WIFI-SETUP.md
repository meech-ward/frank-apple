# Set up Wi-Fi or a hotspot

Settings live in `/wifi.ini` and take effect after a restart. No firmware rebuild
is needed. Without this file, the Apple II works offline.

## 1. Choose a mode

Copy [wifi.example.ini](../wifi.example.ini) to a private `wifi.ini`. Replace its
contents with **one** example below, using your own SSID and passwords.

**Join your home/office network:**

```ini
[wifi]
mode=station
ssid=My WiFi
password=MyWiFiPassword

[ssh]
enabled=true
password=MyOwnSSHPassword
```

**Create a portable hotspot:**

```ini
[wifi]
mode=hotspot
ssid=My Apple II
password=MyHotspotPassword

[ssh]
enabled=true
password=MyOwnSSHPassword
```

The `[wifi]` password joins the network; the `[ssh]` password logs in to the
Apple II. Set `enabled=false` or omit `[ssh]` if you only want browser control.
Keep your filled-in file private.

## 2. Put the file on the device

### Custom Pico 2 W with SD card

1. Power off and put the SD card in your computer.
2. Copy `wifi.ini` to the card root, **beside the `apple` folder**.
3. Eject the card, return it to the Pico, and restart.

### Tufty 2350 badge

For an existing Apple II installation:

1. Extract the [SSH update ZIP](https://github.com/meech-ward/frank-apple/releases)
   or clone this source repo on the computer connected to the badge.
2. Install Python 3, `picotool`, and a native C compiler (`cc`; Xcode Command
   Line Tools on macOS, or your Linux compiler package).
3. Hold **HOME**, tap **RESET**, then release HOME to enter BOOTSEL.
4. From the extracted package/repo directory, run:

   ```sh
   python3 tools/configure-wifi.py --device --wifi /path/to/private/wifi.ini
   ```

5. Keep USB connected until full-flash verification finishes, then tap RESET.

The updater backs up flash in `~/apple2-backups/`, edits only `/wifi.ini`, and
verifies that firmware, saved disks, and `/ssh.key` are unchanged. It compiles a
small helper on your computer, not the firmware. Connect one BOOTSEL device at
a time, or select one with `--serial <picotool-serial>`.

Backups contain passwords and the private host key; keep them private. If an
update is interrupted, return to BOOTSEL and restore **that badge's** backup:

```sh
picotool load --ignore-partitions -v /path/to/backup/flash-before.bin -t bin -o 0x10000000
```

For a wiped badge, first follow [fresh installation](INSTALL.md#tufty-2350).
Do not replace an existing data volume just to change Wi-Fi settings.
The updater's USB flow still needs [physical validation](VALIDATION.md).

## 3. Connect

**Station:** use a 2.4 GHz WPA2-compatible network. Open HOME (F11 with a USB
keyboard) and read the address. Run `ssh apple@<device-ip>`.

**Hotspot:** join your chosen SSID from your laptop, then run
`ssh apple@192.168.4.1`. “No Internet” is normal: the hotspot connects to the
Apple II without routing Internet traffic. Use station mode for Internet APIs.

**Browser:** select **Web control** in the menu and open its displayed URL.
For a hotspot it is `http://192.168.4.1/`. On Tufty, B/C selects and HOME returns;
on a keyboard, Return selects and F11 returns. Web control starts off after each
restart. See [SSH keys and BASIC commands](SSH.md).

## File reference

| Setting | Accepted values |
| --- | --- |
| `[wifi] mode` | `station` (default if omitted) or `hotspot` |
| `ssid` | 1–32 bytes |
| `password` in station mode | 8–63-byte passphrase, 64 hex digits, or explicitly empty for an open network |
| `password` in hotspot mode | 8–63-byte passphrase; required |
| `[ssh] enabled` | `true` or `false` (default) |
| `[ssh] password` | 8–64 bytes; required when enabled |

Use UTF-8 plain text, at most **512 bytes** total and **127 bytes per line**.
CRLF/LF and a UTF-8 BOM are accepted. Full-line comments start with `#` or `;`;
inside values those characters, `=` and backslashes are literal. Matching quotes
preserve leading/trailing spaces. Unknown keys, duplicate sections/keys, or
invalid passwords disable networking. Keep one `[wifi]` and at most one `[ssh]`.

The menu reports missing/invalid files and connection failures. Station mode
retries connection failures; it never falls back to hotspot automatically.
Hotspot mode supplies up to four DHCP leases (`192.168.4.2`–`.5`), without a
router/DNS advertisement. It still permits only one SSH session.

To edit a backup without accessing USB:

```sh
python3 tools/configure-wifi.py --image apple-data.img --wifi /path/to/private/wifi.ini --out apple-data-updated.img
```

Input may be a 12 MiB data image or 16 MiB flash backup; output must be a new file.
