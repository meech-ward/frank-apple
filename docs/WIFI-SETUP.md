# Wi-Fi setup

The public firmware reads `/wifi.ini` when it starts. No Wi-Fi password,
Supabase project, API key, or remote typing channel is built into it.
Networking is optional: without this file, the Apple II starts normally offline.
The same file chooses either your existing Wi-Fi network or the badge's portable
hotspot. You do not need to rebuild firmware to change these settings.

## Choose your connection

To join your home or office network, use:

```ini
[wifi]
mode=station
ssid=My WiFi
password=MyPassword

[ssh]
enabled=true
password=MyOwnSSHPassword
```

Restart, find the IP address in the device menu, then connect from a terminal:

```sh
ssh apple@<device-ip>
```

Enter the password under `[ssh]`. This is separate from the network's password.
The first connection asks you to accept the device's SSH host key.

For a portable connection without a router, use:

```ini
[wifi]
mode=hotspot
ssid=My Apple II
password=MyHotspotPassword

[ssh]
enabled=true
password=MyOwnSSHPassword
```

Restart, join **My Apple II** from your computer's Wi-Fi settings using the
hotspot password, then run:

```sh
ssh apple@192.168.4.1
```

The device provides a 2.4 GHz WPA2 hotspot and assigns your computer an address
automatically. It provides access to the Apple II, not an Internet connection;
your computer may label the network “No Internet.” The hotspot does not advertise
a default router or DNS server, so it does not replace an existing wired Internet
route. BASIC can still make HTTP requests to reachable hosts on the local
hotspot network; use station mode for Internet services. Station and hotspot
are separate startup modes; there is
no automatic fallback that could unexpectedly start a hotspot.

SSH controls the same running Apple II as the physical keyboard. It uses the
fixed username `apple`. Leave out `[ssh]` or use `enabled=false` to keep it off.
Browser control is still available in either network mode when enabled from the
device menu; in hotspot mode its address is `http://192.168.4.1/`.
See [SSH usage](SSH.md) for keyboard controls, writing and saving BASIC programs,
and disconnecting without stopping the Apple II.

## Custom Pico 2 W with SD card

1. Power off the Pico and put its SD card in your computer.
2. Copy `wifi.example.ini` to the card root and rename it `wifi.ini`.
3. Edit it as plain text:

```ini
[wifi]
mode=station
ssid=My WiFi
password=MyPassword
```

4. Eject the card, put it back in the Pico, and restart.
5. Use SSH as described above if you enabled it, or enable browser control.
   With keyboard firmware, press F11, Space, then F11 to enable browser control.
   With USB console firmware, send Ctrl-], Space, Ctrl-]. Open the address shown
   in the disk menu from a computer or phone on the same network.

`wifi.ini` goes alongside the `apple` folder, not inside an Apple disk image.
Changing networks requires only editing that file and restarting. Remove it to
turn Wi-Fi off. The firmware never rewrites it. Firmware-only updates preserve it.

## Tufty 2350 badge

The same file belongs at the root of the badge's internal FAT data volume.
To change an existing badge without replacing your saved programs:

1. Download or clone this source repository on the computer connected to the
   badge. You need Python 3, `picotool`, and a native C compiler (`cc`; Xcode
   Command Line Tools on macOS or your Linux compiler package).
2. Copy `wifi.example.ini` to a private location, rename it `wifi.ini`, and edit
   its station/hotspot and SSH settings as shown above.
3. Connect the badge by USB. Hold HOME, tap RESET, then release HOME to enter
   BOOTSEL mode.
4. From the repository directory, run:

   ```sh
   python3 tools/configure-wifi.py --device --wifi /path/to/private/wifi.ini
   ```

5. Wait for the whole-flash verification to finish, then tap RESET.

The tool backs up all 16 MiB of flash to a new private folder under
`~/apple2-backups/`, edits `/wifi.ini` using the same FatFs code as the firmware,
and compares all other files before updating only the changed data sectors.
Saved disks, firmware, and the device's SSH host key are preserved. This builds
a small helper on your computer; it does not rebuild the firmware. Keep USB
connected until verification finishes. Connect one BOOTSEL device at a time, or
select the badge with `--serial <picotool-serial>`.

These backups contain your saved programs, network passwords, and SSH identity.
Keep them private. If an update is interrupted, leave the badge in BOOTSEL and
restore the recorded `flash-before.bin` with:

```sh
picotool load --ignore-partitions -v /path/to/backup/flash-before.bin -t bin -o 0x10000000
```

Only restore a backup from that same badge; this restores its entire pre-update
state. The updater leaves backups in place even if an operation fails.

For a fresh installation, the [data-image instructions](INSTALL.md#tufty-2350)
can still include `wifi.ini` in the initial data image. Replacing a whole data
image replaces saved programs too; use the updater above for an existing badge.
Firmware-only updates preserve the volume and its configuration.

After connecting, HOME opens the menu. Choose Web control with up/down, press
B or C to enable it, and HOME
returns to the Apple. Web control remains an explicit per-session choice.

The updater also works entirely offline on an existing 12 MiB data image or
16 MiB flash backup, preserving the source file:

```sh
python3 tools/configure-wifi.py --image apple-data.img --wifi /path/to/private/wifi.ini --out apple-data-updated.img
```

This creates a separate image and never accesses a USB device. It is useful for
preparing an image before visiting a badge or checking a configuration change.

## File format

`[wifi]` requires `ssid` and `password`. `mode` is `station` or `hotspot`;
omitting it keeps the existing station behavior, so older files keep working.
Use a 2.4 GHz WPA2-compatible network in station mode. SSIDs can be up to 32
bytes; station passwords can be 8–63 bytes or a 64-digit hexadecimal key. An
explicitly empty `password=` selects an open station network. A missing password
entry is an error. Hotspots always require a WPA2 passphrase of 8–63 bytes;
empty passwords and 64-digit raw keys are not accepted in hotspot mode.

`[ssh]` is optional. `enabled` accepts `true` or `false` and defaults to `false`.
Enabling it requires its own `password` of 8–64 bytes. There is no built-in
password. A configured SSH password shorter than eight bytes is invalid even
when SSH is disabled; remove that line if you do not want to configure it yet.

Full-line comments start with `#` or `;`. Those characters, `=`, and backslashes
inside values are literal. Optional matching single or double quotes preserve
leading or trailing spaces; backslashes are not escape characters. Use UTF-8
plain text; Windows CRLF, Unix LF, and a UTF-8 BOM are accepted. The entire file
must fit in 512 bytes, with at most 127 bytes per line. Unknown/duplicate settings and malformed files disable
Wi-Fi and SSH instead of using partial credentials.

The disk menu distinguishes a missing file, an invalid/unreadable file, a
connection in progress, a rejected password, and a network that cannot be found.
Connection failures retry in the background while the Apple II keeps running.
Credentials are never printed in the console or returned by the browser page.
Keep your filled-in file private; distribute `wifi.example.ini` instead.

The hotspot keeps up to four DHCP leases at `192.168.4.2` through
`192.168.4.5`, each lasting one hour. Its own address is always `192.168.4.1`.
This address pool does not increase the SSH server's single-session limit.
