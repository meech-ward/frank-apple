# Use the badge without a keyboard

Press **HOME** to pause the Apple II and open its launcher. **Up/down** choose
an item, **A** goes back, and **B** or **C** selects it. HOME closes the menu and resumes
the same session. A keyboard's equivalents are F11, arrow keys, Return and Escape.

The menu offers:

- **Resume Apple II**: return to your current program.
- **Saved programs**: browse Applesoft BASIC programs on the DOS 3.3 disk in
  drive 1. Select a name, then **Run program** or **Load without running**.
  Either replaces the program in memory; save any changes first.
- **Run program in memory**: the equivalent of typing `RUN` at the BASIC prompt.
- **Stop program (Ctrl-C)**: send Control-C, the usual BASIC stop key.
- **Choose a disk**: the existing disk menu. Boot a game/application disk, or
  insert a disk when an application asks. The same button controls work here too.
- **Web control: OFF/ON**: select this item with B or C to toggle the local
  web page. Its address appears below the menu.

**A always backs out; B and C both select** throughout the launcher and disk
menus. Read-only is a selectable item on the disk action screen. To refresh
Saved programs, go back with A and open the list again. A keyboard's Space
shortcut still toggles web control at HOME, refreshes Saved programs, and toggles
read-only on the disk action screen. Web control starts off after a restart.
RESET power-off and USB keyboard support are unchanged.

## Save once, launch later

1. Boot your writable DOS 3.3 disk (for example `Workshop.dsk`).
2. Create or enter a program using a USB keyboard or the local web editor.
3. Type `SAVE MY PROGRAM` and wait for disk activity to finish.
4. Later, at the empty `]` prompt, press HOME, choose Saved programs, select
   MY PROGRAM, and choose Run program. No keyboard, Wi-Fi or computer is needed.

The catalog is read from the current working disk, including the most recently
written track. It does not require a separate favorites/configuration file and
does not mount a stale original image. Browsing does not move the emulated drive
head or modify the disk. The startup disk is still the last disk you chose to boot.

If a BASIC program is running, use HOME → Stop program, wait for `]`, then reopen
HOME. Run/load is available only when the existing screen/prompt check recognizes
an empty 40-column Applesoft prompt. It does not send commands into AppleWorks,
VisiCalc, an INPUT question or a partly typed BASIC line. This is a prompt check,
not a guarantee about arbitrary machine-code programs. Control-C may be ignored
by applications, and full-screen graphics can hide BASIC's prompt; use a keyboard
or the web controls to return to text BASIC in those cases.

## Programs that use the front buttons

Outside the menu, A sends Escape, B Return, and C Right, following the same
back/select/forward arrangement:

| Button | Apple key / `ASC` value |
| --- | --- |
| Up | Up arrow / 11 |
| Down | Down arrow / 10 |
| A | Escape / 27 |
| B | Return / 13 |
| C | Right arrow / 21 |

See [`basic/button-counter.bas`](../basic/button-counter.bas) for a small example
using all five. Enter it with the keyboard or web editor and save it as
`BUTTON COUNTER`. Once saved, launch it from the badge menu. Up/down change the
counter, A exits to BASIC, B zeroes it, and C adds ten. Existing games still need
their own supported keys; these buttons do not replace a full keyboard or joystick.

## Scope and checks

The first version lists **Applesoft (A) files on standard 35-track DOS 3.3 disks
in drive 1**. Integer BASIC, binary programs, text/data files and ProDOS catalogs
are not listed; whole application/game disks remain accessible through Choose a
disk. Names must start with A–Z, use printable ASCII, and contain no comma or colon
so they can be entered as a single DOS command. Unreadable/cyclic catalogs produce
an explanatory screen, never a partial launch list. No DOS or applications are
added to the clean installer.

`tools/check-dos-catalog.py` tests the production parser with sanitizers, including
catalog corruption, command delimiters, all sector orders, circular tracks and
unaligned disk writes. Optionally pass a private `.bdsk` to inspect a saved disk.
`tools/check-launcher.py` exercises the production menu and renderer with simulated
storage/input: physical-button and keyboard navigation, run/load, prompt guard,
stop, selectable web control/read-only, snapshot
failures and preservation of Apple RAM. An optional directory receives screen
renders. Host tests do not establish a physical button/launch pass.
