# Disk images and browser journeys

Supply your own disk images; this repository includes no application disks or
personal saves. Put them in `/apple` on SD (Pico) or in the badge's FAT data image.

The Field Guide uses these filenames when selecting an experience:

| Experience | Expected files |
| --- | --- |
| BASIC workshop and our examples | `Workshop.dsk` |
| DOS reference disk | `DOS 3.3 System Master.dsk` |
| VisiCalc | `VisiCalc.dsk`, `VisiCalc Data.dsk` |
| AppleWorks 2.0 | `AppleWorks Startup.po`, `AppleWorks Program.po`, `AppleWorks Data.po` |
| Oregon Trail | `165 Games Oregon Trail.dsk` |

Your Workshop can be a writable, bootable DOS 3.3 disk. Enter the provided
`basic/*.bas` listings using the web editor, then save the names used in the
lessons: BADGE DESK, GUIDE GAME, HTTP DEMO, HTTP API, NET DEMO, STAR TRADER,
SUPA LOGO, and TRIP COST. Prepare application data disks using their normal
formatting workflows. Filenames alone do not make an incompatible version of
an application match a walkthrough.

Without a boot disk, Applesoft BASIC in ROM can still run programs, but DOS
commands such as CATALOG, SAVE and LOAD require DOS to be booted. See
[using the Apple II](USING-THE-APPLE-II.md) for the commands.

Application images and the original Apple ROM contents have separate rights;
the source-code licenses in this repository do not grant rights to those
programs. Upstream ROM arrays are retained with their existing provenance.
