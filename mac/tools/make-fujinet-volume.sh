#!/bin/bash
# Build the "FujiNet" HD20 volume fujinet-nio serves as DCD unit 0, with the
# Mac FujiNet apps on it. fujinet-nio must not be running: it keeps the image
# open and would serve the Mac stale blocks of a volume changed under it.
#
# usage: make-fujinet-volume.sh [image] [blocks]
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
hmount "$IMG" >/dev/null
for app in "$HERE"/apps/*/build/*.bin; do
  [ -f "$app" ] || continue
  name=$(basename "$app" .bin)
  case $name in *.code) continue ;; esac
  hcopy -m "$app" ":$name"
done
hls -l
humount
