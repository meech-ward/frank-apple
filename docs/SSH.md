# Use the Apple II over SSH

SSH, the browser, and the physical keyboard control the same Apple II.
Disconnecting SSH leaves its program running.

## Connect

1. Install an SSH-enabled [firmware update](INSTALL.md).
2. Follow [Wi-Fi setup](WIFI-SETUP.md) to choose your router or a hotspot.
   In `wifi.ini`, **edit the existing `[ssh]` section** to read:

   ```ini
   [ssh]
   enabled=true
   password=MyOwnSSHPassword
   ```

   Choose your own 8–64-byte password. Older files without `[ssh]` need that
   section added once. Duplicate sections make the file invalid.
3. Restart. On your home network, open HOME (or F11 on a keyboard) and use the
   command shown by the SSH row:

   ```sh
   ssh apple@<device-ip>
   ```

   For hotspot mode, join the badge's Wi-Fi first, then run:

   ```sh
   ssh apple@192.168.4.1
   ```

4. Accept the host key on first use and enter the **SSH password**, not the
   hotspot password. Normal macOS/Linux OpenSSH works without cipher options.

Use a terminal at least **80 columns × 25 rows** for the full text view and
help line. The username is always `apple`.

## Write, run, and save a program

At the Applesoft `]` prompt, turn on **Caps Lock** and type or paste:

```basic
NEW
10 FOR I = 1 TO 5
20 PRINT "HELLO FROM SSH ";I
30 NEXT I
RUN
```

`NEW` clears the program in memory. `LIST` shows the source. Replace a line by
retyping its number and contents; type just its number to delete it. `RUN`
starts it again. `Ctrl-C` stops it. `TEXT:HOME` returns to a clean text screen.

With a writable DOS disk booted, save and reload:

```basic
SAVE SSHDEMO
CATALOG
NEW
LOAD SSHDEMO
LIST
RUN
```

`CATALOG` lists saved files. `LOAD` brings a program into memory; `RUN` starts it.
For a long listing, use a range such as `LIST 10,30`. Inside another application,
use **Ctrl-] → Choose a disk**, choose drive 1 and your DOS/workshop disk,
then select its Boot action first.
See [adding software](DISKS.md) if you do not have a DOS disk installed.

## Keys and disconnecting

| Key | Action |
| --- | --- |
| Return | Apple Return |
| Backspace / Delete | Apple left/backspace |
| Arrows / Escape | Apple arrows / Escape |
| Ctrl-C | Stop BASIC and cancel queued typing |
| Ctrl-] or F11 | Open/close the device menu |
| Menu: arrows, Return, Escape | Move, select, back |

To disconnect, press **Return**, then **`~.`** (tilde, period). Return reaches
the Apple; OpenSSH handles `~.` locally. Reconnect with the same command.

Input preserves case: use uppercase Applesoft keywords and straight ASCII
quotes. Unicode and Open Apple modifier shortcuts are not supported. Graphics
stay on the badge; SSH shows text, inverse highlighting, and mixed-mode text.
The cursor position is approximate in full-screen applications.

## Turn SSH off or troubleshoot

- Set `[ssh] enabled=false` and restart. With a password configured, the menu's
  SSH row toggles access until the next restart.
- **Cannot connect:** check the menu's IP/status and that your laptop is on the
  same network. A second SSH client is rejected while the first is connected.
- **Hotspot says “No Internet”:** expected. It connects your laptop to the badge.
- **Disconnected after rekey:** reconnect. The Apple II is still running.
- **Host key changed after intentionally wiping or switching badges:** confirm
  which badge you joined, run `ssh-keygen -R 192.168.4.1` (or its station IP),
  then reconnect. Firmware-only updates preserve the key and should not cause this.
- **SSH setup failed:** the menu shows the storage/key error. Restore a damaged
  `/ssh.key` from that badge's backup before attempting another connection.

This provides one interactive connection, with no Unix shell, SFTP, remote
commands, or forwarding. [How SSH works](SSH-INTERNALS.md) explains the implementation.
