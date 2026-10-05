#!/usr/bin/env python3
"""Update Tufty /wifi.ini while preserving firmware, disks, and SSH host identity.

Needs Python 3 and a native C compiler (cc). Device mode also needs picotool and
a Tufty in BOOTSEL mode. Offline image mode never accesses USB. Private backup
and output images contain your credentials; do not publish them.
"""
import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FLASH_BASE = 0x10000000
DATA_OFFSET = 4 * 1024 * 1024
FLASH_SIZE = 16 * 1024 * 1024
DATA_SIZE = FLASH_SIZE - DATA_OFFSET
ERASE_SIZE = 4096


def run(command, *, timeout=120):
    result = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
    if result.returncode:
        detail = (result.stderr or result.stdout).strip()
        raise RuntimeError(f"{Path(command[0]).name} failed: {detail}")
    return result.stdout.strip()


def build_helper(directory, compiler="cc", sanitize=False):
    executable = shutil.which(compiler)
    if not executable:
        raise RuntimeError("A native C compiler is needed: install Xcode Command Line Tools on macOS or your Linux C compiler package.")
    fat = ROOT / "drivers/fatfs"
    output = directory / "wifi-image-edit"
    flags = ["-g", "-fsanitize=address,undefined"] if sanitize else ["-O2"]
    run([executable, "-std=c11", *flags, "-I", str(ROOT / "src"), "-I", str(fat),
         str(ROOT / "tools/wifi-image-edit.c"), str(ROOT / "src/wifi_config.c"),
         *[str(fat / name) for name in ("ff.c", "ffsystem.c", "ffunicode.c")],
         "-o", str(output)])
    return output


def private_write(path, data):
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "wb") as output:
        output.write(data)
        output.flush()
        os.fsync(output.fileno())


def prepare_image(image, wifi, directory, helper):
    if len(image) == FLASH_SIZE:
        prefix, data = image[:DATA_OFFSET], image[DATA_OFFSET:]
    elif len(image) == DATA_SIZE:
        prefix, data = b"", image
    else:
        raise ValueError("Expected a 12 MiB data image or a full 16 MiB Tufty flash backup.")
    source, destination = directory / "data-before.img", directory / "data-after.img"
    private_write(source, data)
    message = run([str(helper), str(source), str(wifi), str(destination)])
    result = destination.read_bytes()
    if len(result) != DATA_SIZE:
        raise RuntimeError("Updated data image has an unexpected size.")
    return prefix + result, message


def changed_ranges(before, after):
    """Only full, aligned flash erase sectors in the reserved data volume."""
    if len(before) != FLASH_SIZE or len(after) != FLASH_SIZE:
        raise ValueError("A device update requires two complete Tufty flash images.")
    if before[:DATA_OFFSET] != after[:DATA_OFFSET]:
        raise ValueError("Refusing an update that changes firmware.")
    ranges = []
    for offset in range(DATA_OFFSET, FLASH_SIZE, ERASE_SIZE):
        if before[offset:offset+ERASE_SIZE] == after[offset:offset+ERASE_SIZE]:
            continue
        if ranges and ranges[-1][1] == offset:
            ranges[-1] = (ranges[-1][0], offset + ERASE_SIZE)
        else:
            ranges.append((offset, offset + ERASE_SIZE))
    return ranges


def update_device(wifi, backup_directory, helper, work, picotool="picotool", serial=None):
    executable = shutil.which(picotool)
    if not executable:
        raise RuntimeError("picotool is required for a connected Tufty update.")
    selection = ["--ser", serial] if serial else []
    backup_directory.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    backup_directory.mkdir(mode=0o700, exist_ok=False)
    before_path = backup_directory / "flash-before.bin"
    print(f"Saving and verifying a private full-flash backup in {backup_directory}", flush=True)
    run([executable, "save", "-a", "-v", str(before_path), "-t", "bin", *selection])
    before_path.chmod(0o600)
    # picotool inherits this program's restrictive umask. fsync before any write.
    with before_path.open("rb") as backup:
        os.fsync(backup.fileno())
    before = before_path.read_bytes()
    if len(before) != FLASH_SIZE:
        raise ValueError("Connected board is not a Tufty with 16 MiB flash. Backup kept; no writes made.")
    after, message = prepare_image(before, wifi, work, helper)
    after_path = backup_directory / "flash-expected.bin"
    private_write(after_path, after)
    ranges = changed_ranges(before, after)
    plan = []
    for index, (start, end) in enumerate(ranges):
        path = backup_directory / f"changed-{index:02d}.bin"
        data = after[start:end]
        private_write(path, data)
        plan.append({"file": path.name, "address": hex(FLASH_BASE+start), "bytes": len(data),
                     "sha256": hashlib.sha256(data).hexdigest()})
    private_write(backup_directory / "update.json", (json.dumps({
        "before_sha256": hashlib.sha256(before).hexdigest(),
        "expected_sha256": hashlib.sha256(after).hexdigest(),
        "ranges": plan,
    }, indent=2) + "\n").encode())
    # Recheck the connected board before writing; no force-reset or auto reboot.
    run([executable, "verify", str(before_path), "-t", "bin", "-o", hex(FLASH_BASE), *selection])
    print(message, flush=True)
    print(f"Writing {sum(end-start for start, end in ranges)//ERASE_SIZE} changed data sectors; keep USB connected.", flush=True)
    for item in plan:
        run([executable, "load", "--ignore-partitions", "-v",
             str(backup_directory / item["file"]), "-t", "bin", "-o", item["address"], *selection])
    run([executable, "verify", str(after_path), "-t", "bin", "-o", hex(FLASH_BASE), *selection])
    print("Verified the entire flash image. Firmware and saved programs are preserved. Tap RESET to restart.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wifi", type=Path, required=True, help="Your private edited wifi.ini")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--image", type=Path, help="Existing 12 MiB data image or full 16 MiB flash backup (offline)")
    source.add_argument("--device", action="store_true", help="Update a connected Tufty in BOOTSEL mode")
    parser.add_argument("--out", type=Path, help="New output image for offline mode; existing files are never overwritten")
    parser.add_argument("--backup-dir", type=Path, help="New private directory for device backups; defaults to ~/apple2-backups/<time>")
    parser.add_argument("--serial", help="Select a specific BOOTSEL device by picotool serial number")
    parser.add_argument("--picotool", default="picotool", help="picotool executable path")
    parser.add_argument("--cc", default="cc", help="Native C compiler executable path")
    args = parser.parse_args()
    if args.image and (not args.out or args.backup_dir or args.serial):
        parser.error("--image requires --out and cannot use --backup-dir or --serial")
    if args.device and args.out:
        parser.error("--out is only for offline --image mode")
    # Source validation runs before USB access. New files, including backups
    # created by picotool, are accessible only to the current user by default.
    old_umask = os.umask(0o077)
    try:
        with tempfile.TemporaryDirectory(prefix="apple2-config-") as temporary:
            work = Path(temporary)
            helper = build_helper(work, args.cc)
            wifi = args.wifi.resolve()
            run([str(helper), "--validate", str(wifi)])
            if args.image:
                if args.out.exists():
                    raise ValueError("Output already exists; choose a new path to preserve both images.")
                after, message = prepare_image(args.image.read_bytes(), wifi, work, helper)
                private_write(args.out, after)
                print(f"{message}\nCreated {args.out}; source image is unchanged. No device accessed.")
            else:
                backup = args.backup_dir or Path.home() / "apple2-backups" / datetime.now().strftime("%Y%m%d-%H%M%S-%f")
                update_device(wifi, backup.resolve(), helper, work, args.picotool, args.serial)
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        parser.exit(1, f"Error: {error}\nAny existing backup is retained. The device has not been rebooted.\n")
    finally:
        os.umask(old_umask)


if __name__ == "__main__":
    main()
