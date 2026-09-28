#!/bin/bash
# Build the "FujiNet" HD20 volume fujinet-nio serves as DCD unit 0, with the
# Mac FujiNet apps on it. fujinet-nio must not be running: it keeps the image
# open and would serve the Mac stale blocks of a volume changed under it.
#
# usage: make-fujinet-volume.sh [image] [blocks]
#   BOOT_FROM=<bootable HFS floppy image>  also copy its boot blocks and
#   System Folder and bless it, so the Mac starts up from the HD20.
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
IMG=${1:-$HERE/run/fujinet-data/mac/FujiNet.hda}
BLOCKS=${2:-16384}                       # 8 MB; HD20 volumes must stay <= 65519
RETRO68_BUILD=${RETRO68_BUILD:-$HOME/code/Retro68-build}
export PATH=$RETRO68_BUILD/toolchain/bin:$PATH

if pgrep -f "mac-floppy-tcp-debug/fujinet-nio" >/dev/null; then
  echo "stop fujinet-nio first (it holds $IMG open)" >&2
  exit 1
fi

mkdir -p "$(dirname "$IMG")"
dd if=/dev/zero of="$IMG" bs=512 count="$BLOCKS" 2>/dev/null
hformat -l FujiNet "$IMG" >/dev/null

if [ -n "${BOOT_FROM:-}" ]; then
  # The boot blocks (blocks 0-1) name the System and Finder to launch.
  dd if="$BOOT_FROM" of="$IMG" bs=512 count=2 conv=notrunc 2>/dev/null
  TMP=$(mktemp -d)
  hmount "$BOOT_FROM" >/dev/null
  for f in System Finder; do hcopy -m ":System Folder:$f" "$TMP/$f.bin"; done
  humount
  hmount "$IMG" >/dev/null
  hmkdir ":System Folder"
  for f in System Finder; do hcopy -m "$TMP/$f.bin" ":System Folder:$f"; done
  hattrib -b ":System Folder"
  humount
  rm -rf "$TMP"
fi

hmount "$IMG" >/dev/null
for app in "$HERE"/apps/*/build/*.bin; do
  [ -f "$app" ] || continue
  name=$(basename "$app" .bin)
  case $name in *.code) continue ;; esac
  hcopy -m "$app" ":$name"
done
hls -l
humount

# An 800K floppy for NIO's floppy slot (5), with the apps too.
FLOPPY=$(dirname "$IMG")/Floppy800.dsk
dd if=/dev/zero of="$FLOPPY" bs=1024 count=800 2>/dev/null
hformat -l "NIO Floppy" "$FLOPPY" >/dev/null
hmount "$FLOPPY" >/dev/null
for app in "$HERE"/apps/*/build/*.bin; do
  [ -f "$app" ] || continue
  name=$(basename "$app" .bin)
  case $name in *.code) continue ;; esac
  hcopy -m "$app" ":$name"
done
humount

# Forget runtime mounts from earlier runs: slot 0 comes from the config.
rm -f "$(dirname "$IMG")/../fujinet-runtime-mounts.tsv"
