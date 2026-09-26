#!/usr/bin/env bash
# Build the raw FAT16 image that lives in the Tufty 2350's flash (drivers/flashdisk).
#   tools/mkfatimg.sh <out.img> <size MiB> <dir with .dsk files> [wifi.ini]
# The image gets one folder, /apple, holding every file from <dir>. Flash it at
# 0x10000000 + FLASHDISK_OFFSET (0x10400000): see docs/INSTALL.md.
set -euo pipefail
shopt -s nullglob
OUT=${1:?out.img}; MIB=${2:?size MiB}; SRC=${3:?dir with disk images}
MNT=$(mktemp -d /tmp/mkfat.XXXXXX)
rm -f "$OUT"
dd if=/dev/zero of="$OUT" bs=1048576 count="$MIB" status=none
DEV=$(hdiutil attach -imagekey diskimage-class=CRawDiskImage -nomount "$OUT" | awk 'NR==1{print $1}')
newfs_msdos -F 16 -b 2048 -v APPLE2 "$DEV" >/dev/null
hdiutil detach "$DEV" >/dev/null
hdiutil attach -imagekey diskimage-class=CRawDiskImage -mountpoint "$MNT" "$OUT" >/dev/null
mkdir -p "$MNT/apple"
n=0
for f in "$SRC"/*; do
  case "$(basename "$f")" in ._*|.DS_Store) continue;; esac
  cp "$f" "$MNT/apple/"; n=$((n+1))
done
if [[ -n "${4:-}" ]]; then cp "$4" "$MNT/wifi.ini"; fi
sync
ls -la "$MNT/apple"
# strip Finder metadata the copy may have added
find "$MNT" -name '._*' -delete 2>/dev/null || true
rm -rf "$MNT/.fseventsd" "$MNT/.Spotlight-V100" "$MNT/.Trashes" 2>/dev/null || true
hdiutil detach "$MNT" >/dev/null
rmdir "$MNT" 2>/dev/null || true
echo "wrote $OUT ($MIB MiB, $n files in /apple)"
