# Wi-Fi setup

The public firmware reads `/wifi.ini` when it starts. No Wi-Fi password,
Supabase project, API key, or remote typing channel is built into it.
Networking is optional: without this file, the Apple II starts normally offline.

## Custom Pico 2 W with SD card

1. Power off the Pico and put its SD card in your computer.
2. Copy `wifi.example.ini` to the card root and rename it `wifi.ini`.
3. Edit it as plain text:

```ini
[wifi]
ssid=My WiFi
password=MyPassword
```

4. Eject the card, put it back in the Pico, and restart.
5. With keyboard firmware, press F11, Space, then F11 to enable browser control.
   With USB console firmware, send Ctrl-], Space, Ctrl-]. Open the address shown
   in the disk menu from a computer or phone on the same network.

`wifi.ini` goes alongside the `apple` folder, not inside an Apple disk image.
Changing networks requires only editing that file and restarting. Remove it to
turn Wi-Fi off. The firmware never rewrites it. Firmware-only updates preserve it.

## Tufty 2350 badge

The same file belongs at the root of the badge's internal FAT data volume.
The badge does not expose a USB configuration drive. Follow the
[data-image instructions](INSTALL.md#tufty-2350) to include `wifi.ini` in a fresh
installation. Replacing the data image replaces saved programs too. Firmware-only
updates preserve an existing volume and its Wi-Fi file.

After connecting, HOME opens the menu. Choose Web control with up/down, press
B or C to enable it, and HOME
returns to the Apple. Web control remains an explicit per-session choice.

## File format

Use a 2.4 GHz WPA2-compatible network. SSIDs can be up to 32 bytes; passwords
can be 8–63 characters or a 64-digit hexadecimal key. An explicitly empty
`password=` selects an open network. A missing password entry is an error.

Full-line comments start with `#` or `;`. Those characters, `=`, and backslashes
inside values are literal. Optional matching single or double quotes preserve
leading or trailing spaces; backslashes are not escape characters. Use UTF-8
plain text; Windows CRLF, Unix LF, and a UTF-8 BOM are accepted. The entire file
must fit in 512 bytes, with at most 127 bytes per line. Unknown/duplicate settings and malformed files disable
Wi-Fi instead of using partial credentials.

The disk menu distinguishes a missing file, an invalid/unreadable file, a
connection in progress, a rejected password, and a network that cannot be found.
Connection failures retry in the background while the Apple II keeps running.
Credentials are never printed in the console or returned by the browser page.
Keep your filled-in file private; distribute `wifi.example.ini` instead.
