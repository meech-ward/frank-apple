# Using your Apple II

A plain guide to using this machine the way an Apple II owner did in 1980, plus the few
things that are different because the floppy disks are image files. On the Tufty badge,
those files live in its internal flash; on the Pico Display Pack build they live on microSD.

## Tufty badge controls

For guided lessons and a virtual keyboard, enable the local browser Field Guide.
Choose **Start my first session** for one action at a time, then try creating a
game or exploring classic applications. Supply the [required disks](DISKS.md)
for the application journeys.
On the HOME launcher screen, choose Web control with up/down and press B or C.
Its local WiFi address appears below the menu.
Open that address on your computer or phone, then press HOME to return to the Apple.
The web server is off after every reset. USB console debugging is available in
the console build; the keyboard build uses that USB port to host a keyboard.

The badge boots DOS automatically from the last disk mounted in drive 1. Its buttons work
without a keyboard: HOME opens or closes the launcher, UP/DOWN move through it,
A goes back and B or C selects. Saved programs lists Applesoft programs on the
DOS 3.3 disk in drive 1; Choose a disk opens the disk menu. Read-only is a
selectable item on the disk action screen. Outside the menu, A sends Escape,
B Return, C Right, and up/down send their arrows to the Apple II.
See the [button launcher](BUTTON-LAUNCHER.md).

The case LEDs show drive activity. After SAVE, wait for the `]` prompt and for the drive
indicator to turn off before resetting or powering down. A save may take several seconds;
counting three seconds from pressing Return is too short. CATALOG is not required after SAVE.

With [wifi.ini configured](WIFI-SETUP.md), Wi-Fi connects in the background and
retries automatically. At the BASIC prompt, `PRINT PEEK(49296)` reports 1 when
connected. Networking works locally; no cloud account is required.

The USB console firmware lets the Mac type into the badge. The USB keyboard firmware uses
the same port to host a keyboard: connect the powered USB-C OTG keyboard setup after flashing.
Returning from keyboard firmware to console firmware requires BOOT plus RESET or a Debug Probe.

The badge has no SD slot. Replacing its complete flash disk image also replaces the programs
saved inside it. Keep a flash backup before doing that. Wi-Fi supplies [HTTP/HTTPS requests](HTTP-GET.md) and the optional local browser
controller. It does not synchronize saved disks.


## 1. Power on

Plug in the 5 V feed. The screen shows the Apple //e title for a moment, then:

- **If a disk was left "in the drive"**, it boots that disk, exactly like a real Apple II
  with a floppy in drive 1. For DOS 3.3 you get the System Master banner and a `]` prompt.
- **If no disk is remembered**, you get an empty screen with `]`. That is Applesoft BASIC
  running from ROM. You can program right there, but you can't load or save anything.

The machine remembers the last disk you booted from the menu. To change it, boot a different
one from the menu. To forget it, delete `autoboot.txt` from the `apple` folder on the card.

## 2. The disk drive (F11)

There is no drive door, so the keyboard's F11 key is your hands on the disk:

| Key | Does |
|---|---|
| F11 | Open or close the disk menu |
| 1 or 2 | Pick drive 1 or drive 2. Drive 1 is the one that boots. |
| Arrows | Move through the list. `..` at the top goes up one folder. |
| Enter | Choose the highlighted disk, then choose an action |
| Enter on **Boot** | Put the disk in and restart the machine, like flipping the power |
| Enter on **Insert** | Swap the disk without restarting, for programs that ask for disk 2 |
| Space | Tick or untick Read-only for that disk |
| Esc | Back |
| F12 (hold) | Turbo, for skipping long loads |
| Ctrl+Alt+Delete | Reset, the Apple's Ctrl-Reset. Gets you out of a stuck program. |

Typing `PR#6` at the `]` prompt also reboots whatever disk is in drive 1.

## 3. Putting disks on the card

The card is FAT32 with one folder, `apple`. Every disk image in that folder shows up in the
menu. Copy files from the Mac and eject the card. Nothing to install.

- Formats that work: `.dsk` `.do` `.po` `.nib` `.woz`. Prefer `.dsk`.
- Supply your own disk images; see [disk preparation](DISKS.md).
- Files the firmware makes for itself: `NAME.dsk.bdsk` next to each disk (its working copy,
  where your saves go; the original `.dsk` stays untouched) and `autoboot.txt`.
- If the menu shows an entry twice, Finder left a `._NAME.dsk` companion file. Delete it.

A blank disk to save things onto: on the Mac run

```bash
dd if=/dev/zero of=BLANK.dsk bs=1024 count=140
```

copy it to the card, boot DOS from the System Master, choose BLANK with **Insert**, and type
`INIT HELLO`. It is now a formatted DOS disk that boots on its own.

## 4. Playing a game

To play a game, follow the original workflow:
pick the game's disk, boot it, play, and to play something else pick another disk and boot again.

F11, `1`, arrow to the game, Enter, **Boot**. The drive grinds for a few seconds and the title
screen comes up. Most games are self-booting and never show a `]` prompt. Ctrl+Alt+Delete when
you want out.

There are no paddles or joystick, so games that need them won't be playable yet. Games that use
the keyboard are fine.

## 5. Using DOS 3.3

Boot the System Master and you're at `]`. DOS 3.3 adds disk commands to the Applesoft BASIC environment.
Type in CAPITALS. Lowercase words give `?SYNTAX ERROR`, so leave Caps Lock on.

| Command | Does |
|---|---|
| `CATALOG` | List the disk. It pauses when the screen is full; any key continues. |
| `RUN NAME` | Load and run a BASIC program (type `A` or `I` in the catalog) |
| `BRUN NAME` | Run a machine-language program (type `B`) |
| `LOAD NAME` then `LIST` | Look at a program's source |
| `SAVE NAME` | Save the program in memory. Overwrites a file of the same name. |
| `DELETE NAME` | Delete. `LOCK NAME` / `UNLOCK NAME` protect against that. |
| `RENAME OLD,NEW` | Rename |
| `INIT HELLO` | Format the disk in the drive and make the current program its boot program |
| `PR#6` | Reboot the disk |
| `FP` / `INT` | Select Applesoft (floating point) or Integer BASIC, if loaded by your DOS disk. |
| `CALL -151` | The machine-language monitor. `*` prompt. Ctrl-C then Enter to leave. |

Catalog letters: `A` Applesoft program, `I` Integer BASIC program, `B` binary, `T` text file.
The number is the size in sectors. A `*` means the file is locked.

The System Master itself is worth exploring: use `CATALOG` to see its programs.

## 6. Writing your own program

Nobody installed a development environment. You typed at the prompt:

```
NEW
10 HGR : HCOLOR=3
20 FOR X = 0 TO 279 STEP 4
30 HPLOT X,0 TO 279-X,159
40 NEXT
50 FOR I = 1 TO 50 : S = PEEK(-16336) : NEXT
60 GOTO 10
RUN
```

Ctrl+C stops it. `LIST` shows it. `SAVE LINES` keeps it. Line 50 clicks the speaker once the
amplifier is wired up. Type a line number on its own to delete that line.

Useful reading, all free:

- [Applesoft BASIC Programming Reference Manual](https://archive.org/details/applesoft-ii-ref):
  every statement with examples. The book most people learned from.
- [DOS User's Manual](https://archive.org/details/dos-users-manual-for-ii-ii-iie): the disk
  commands above, in more depth.
- `PEEK(-16384)` reads the keyboard, `POKE -16368,0` clears it, `PEEK(-16336)` clicks the
  speaker, `HGR`, `HCOLOR`, `HPLOT` draw, `TEXT` goes back to text.

**Sending a program from your computer.** The browser editor waits for BASIC
between lines and provides Stop and a text-screen view. It preserves case inside
quoted strings. Wait for entry to finish before RUN. Programs live in memory
until you SAVE them to a writable DOS disk.

**Serious tooling.** cc65 compiles C and assembles 6502 for the Apple II and gives you a file to
`BRUN`. AppleCommander writes files into a `.dsk` image on the Mac, including BASIC from a text
file. Build the disk, copy it to the card, boot it.

**Your program as the boot program.** `INIT HELLO` on a blank disk while your program is in
memory makes that disk boot straight into it, and autoboot makes the machine power on into it.

## 7. When something goes wrong

- **Stuck program**: Ctrl+Alt+Delete. If DOS was running you land back at `]` with DOS intact.
- **`?SYNTAX ERROR`**: usually lowercase. Caps Lock.
- **`FILE NOT FOUND`**: `CATALOG` and check the spelling; names can have spaces.
- **`WRITE PROTECTED`**: the disk was mounted with Read-only ticked. F11, choose it again with
  the box unticked.
- **`I/O ERROR`** on a downloaded disk: the image is damaged or copy-protected. Try another copy.
- **Menu says "No disk images found"**: the files aren't in the `apple` folder, or they have an
  extension the firmware doesn't know.
- **Nothing boots at power-on**: no disk is remembered yet. Boot one from F11 once.
- **Saved something and it's gone**: wait for SAVE to return to `]` and for the drive
  indicator to turn off before switching off. Power loss during an active flash write can
  damage the saved data.
