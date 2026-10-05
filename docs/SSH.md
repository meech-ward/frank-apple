# Use your Apple II over SSH

SSH connects your computer's terminal to the same Apple II running on the badge
or Pico. Type on your laptop and watch the Apple screen respond. The physical
keyboard, browser controller, and SSH all operate that one machine. Disconnecting
does not reset it or stop a running program.

## Connect

Use a firmware build with SSH support, then add the following to your private
`wifi.ini` alongside its `[wifi]` settings:

```ini
[ssh]
enabled=true
password=MyOwnSSHPassword
```

Choose your own password of 8–64 bytes. The username is always `apple`; there is
no shared default password. See [Wi-Fi setup](WIFI-SETUP.md) to edit the SD-card
file or update an existing Tufty while preserving saved programs.

For your home or office network, use `[wifi] mode=station`. Restart, open the
device menu, and look at the SSH row for the connection command:

```sh
ssh apple@<device-ip>
```

For portable use, set `[wifi] mode=hotspot` and choose its `ssid` and `password`.
Join that Wi-Fi network from your computer, then connect:

```sh
ssh apple@192.168.4.1
```

The hotspot password joins the Wi-Fi network; the `[ssh]` password logs in.
The hotspot supplies a local connection to the Apple II without Internet
sharing. It does not need an existing router.

Your normal OpenSSH client on macOS or Linux works without special cipher flags.
Accept the device's host key on the first connection, then enter your SSH
password. Use a terminal window at least 80 columns by 25 rows for the full
text screen and help line.

## Write something

At the Applesoft `]` prompt, turn on **Caps Lock** and type:

```basic
NEW
10 FOR I = 1 TO 5
20 PRINT "HELLO FROM SSH ";I
30 NEXT I
RUN
```

`NEW` clears the program currently in memory. `LIST` displays your source.
Replace a line by typing its number and new contents, then `RUN` again. Typing
only a line number deletes that line. You can also paste a small ASCII BASIC
program; input waits for the Apple keyboard queue instead of being dropped.

On a booted writable DOS disk, save and reload with:

```basic
SAVE SSHDEMO
CATALOG
NEW
LOAD SSHDEMO
LIST
RUN
```

If you are inside another application, open the device menu with **Ctrl-]**.
Use up/down and Return to choose your workshop disk or a saved program. The menu
is shared with the badge buttons and browser controller too.

## Keyboard controls

| Your terminal | Apple II action |
| --- | --- |
| Return | Return |
| Backspace or Delete | Left/backspace key |
| Arrow keys | Apple direction keys |
| Escape | Escape |
| Ctrl-C | Interrupt BASIC and cancel queued typing |
| Ctrl-] or F11 | Open or close the device menu |

SSH preserves the case you type. Applesoft commands and keywords need uppercase,
so Caps Lock is convenient. Text applications can use lowercase normally.
The connection sends ASCII keyboard input; unsupported Unicode characters are
ignored. Application shortcuts that need an Open Apple modifier are not mapped
in this first version.

To disconnect, press **Return**, then type **`~.`** (tilde, period). This is
OpenSSH's normal disconnect shortcut. The Return is sent to the Apple; the
shortcut itself is handled by your SSH client. Reconnect later with the same
command to see the Apple II's current screen.

## Scope and identity

This is a single interactive Apple II connection. It mirrors 40/80-column text
and inverse highlighting. Graphics continue to appear on the physical display;
the text portion of mixed graphics modes remains visible remotely. Cursor
placement is approximate in applications that manage their own full-screen
cursor.

It does not provide a Unix shell, remote command execution, SFTP/SCP, port
forwarding, or separate sessions. One SSH client can connect at a time. For long
connections, a client rekey request or the firmware's session limits closes the
connection; reconnect to continue using the same running Apple II.

The device creates its unique SSH host identity in `/ssh.key` on first use.
Firmware-only and Wi-Fi configuration updates preserve it. Keep this file and
full-flash backups private, and do not distribute an already configured badge's
data image as a fresh install for other people. An unreadable or damaged key
disables SSH rather than silently replacing the identity.

Set `enabled=false` to leave SSH off after a restart. With a password configured,
the SSH row in the device menu can enable or disable it for the current session.
The physical keyboard continues working either way.
