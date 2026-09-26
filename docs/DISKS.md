# Add software to your Apple II

You can add existing Apple II software or disks you make yourself. The firmware
and clean installer include no historical application or operating-system disks.
A disk image is a file containing an entire Apple floppy; it is different from a
BASIC text listing.

## Add a disk from your computer

1. Download or make a compatible disk image. Unzip it on your computer first.
2. Open the badge's local Field Guide and choose **Add software**.
3. Choose one or more disk files. Review the names, then choose **Add selected disks**.
4. Wait for “Added.” Your current Apple session keeps running during the upload.
5. Find the disk under **Your disks** and choose **Boot** to start it. Save your
   current work first: booting replaces the program in memory.

The same browser flow works with Tufty internal storage and Pico SD storage.
Nothing needs recompiling. The device must have its data volume/SD and Wi-Fi set
up, with Web control enabled. See [installation](INSTALL.md).

**Insert in Drive 1** swaps disks without restarting an application. Use it when
an app asks for its program disk or a game requests another side. **Insert in
Drive 2** is useful for documents/data. A disk containing only files may not be
bootable: start its application or DOS first, then insert the data disk.

Existing files and saved working copies are never replaced by an upload. Choose
a different name to keep two versions. Interrupted uploads stay out of the disk
list; unfinished sessions expire after a minute. If the connection stopped during
the final step, refresh the list to see whether the disk was added before retrying.
Leave room for working copies and saves; the importer reserves at least 512 KB.

## Supported images

- `.dsk`, `.do`, `.po`: standard 140 KB (143,360-byte), 35-track floppy images.
- `.nib`: 232,960-byte nibble images.
- `.woz`: compatible WOZ1/WOZ2 5.25-inch images, up to 1 MiB, within the emulator's
  35-track/6656-byte-per-track limits. Some protected disks require layouts this
  emulator cannot represent; the importer rejects those layouts.
- `.bdsk`: this emulator's version-1, 35-track working disk format.

The importer checks sizes and WOZ/BDSK structure. It cannot guarantee that every
application boots or supports the emulated IIe. It does not import ZIP files,
3.5-inch images, hard-disk images, or Apple IIgs-only programs. Keep the original
extension: renaming `.dsk` to `.po` does not convert its sector order.

Use an ASCII filename of up to 58 characters. Names can contain spaces. A Pico
user can alternatively power down, put the SD card in their computer, and copy
the images into `/apple`; reinsert the card before powering up. Preserve `.bdsk`
working files when backing up, because they contain saved changes.

## Optional classic examples

These are the exact versions used to develop our walkthroughs. Obtain application
files separately; the emulator's license does not grant rights to archive content.

| Example | Download source | Files to choose | Names used by the guide |
| --- | --- | --- | --- |
| VisiCalc | [Brutal Deluxe](https://www.brutaldeluxe.fr/projects/visicalc/index.html) | `VISICALC.DSK`, the 140 KB image | `VisiCalc.dsk` |
| AppleWorks 2.0 | [Apple II archive](https://mirrors.apple2.org.za/ftp.apple.asimov.net/images/productivity/integrated/appleworks/v2.0_apple/) | `AppleWorks 2.0 Disk 1 Boot - Apple 1986.po` and `AppleWorks 2.0 Disk 2 Program - Apple 1986.po` | `AppleWorks Startup.po`, `AppleWorks Program.po` |

The importer suggests those guide filenames automatically. It does not download
or bundle the applications. For AppleWorks, Boot the startup disk, then Insert
the program disk in Drive 1 when asked.

For saving, use **Create an empty data disk** in the library. This creates zeroed
media with no DOS/ProDOS or other software. Initialize it using the application:

- VisiCalc: name it `VisiCalc Data.dsk`, insert in Drive 2, then use `/SI`, Escape
  to remove the default drive number, `2`, Return. Verify slot 6, drive 2.
- AppleWorks: name it `AppleWorks Data.po`, insert in Drive 2, then use Other
  Activities → Format a disk. Select Disk 2 and a volume name such as WORKDESK.

Formatting erases the selected disk. Only initialize a new empty disk; skip it
when reusing a disk with saved work.

## Your own BASIC programs

Choose **Bring your own BASIC source** to open a `.bas` or `.txt` listing in the
browser editor. Review it, then choose **Enter this program**. This replaces only
the program in Apple memory after confirmation. The importer accepts numbered
ASCII source lines up to 238 characters, in files up to 64 KB. It does not read a
tokenized BASIC file directly; keep those files inside their disk image.

The `basic/` folder contains our original examples. The editor also accepts pasted
code. Once entered, `RUN` runs it and `LIST` shows its source. To use `SAVE`, `LOAD`
and `CATALOG`, first boot a DOS disk you supply. A writable DOS 3.3 image named
`Workshop.dsk` matches the guide's programming lessons. Save STAR TRADER and the
other provided listings onto it before lessons that load those filenames.

A newly installed device can run ROM BASIC without DOS. Application disks and
DOS are optional imports, not dependencies hidden inside the clean installer.
The inherited Apple ROM arrays remain in firmware under their existing provenance;
this disk-import feature does not change their licensing.
