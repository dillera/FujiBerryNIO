#!/bin/bash
# Build a bootable HD20 volume: the boot blocks and the whole System Folder
# of a bootable image, the FujiNet apps, and a "<volume> Files" folder of text
# files for drive-to-drive copy tests. Each line of a test file names the
# volume, the file and the line number, so a bad copy is easy to see.
#
# usage: make-system-volume.sh image blocks "Volume Name" [boot source]
#   blocks: at most 65519 (the Mac's HD20 limit less NIO's mailbox)
#   boot source: a bootable HFS image (default run/FujiNet51-config.hda)
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
IMG=$1 BLOCKS=$2 LABEL=$3
SRC=${4:-$HERE/run/FujiNet51-config.hda}
export PATH=${RETRO68_BUILD:-$HOME/code/Retro68-build}/toolchain/bin:$PATH
export LC_ALL=C                    # System Folder names are MacRoman
[ "$BLOCKS" -le 65519 ] || { echo "at most 65519 blocks" >&2; exit 1; }
TMP=$(mktemp -d)
trap 'humount >/dev/null 2>&1 || true; rm -rf "$TMP"' EXIT

# the System Folder, by index: hcopy names the files from MacBinary
hmount "$SRC" >/dev/null
hls -1 ':System Folder' > "$TMP/list"
i=0
while IFS= read -r f; do
  i=$((i + 1))
  hcopy -m ":System Folder:$f" "$TMP/sys$i.bin"
done < "$TMP/list"
humount >/dev/null

dd if=/dev/zero of="$IMG" bs=512 count="$BLOCKS" 2>/dev/null
hformat -l "$LABEL" "$IMG" >/dev/null
dd if="$SRC" of="$IMG" bs=512 count=2 conv=notrunc 2>/dev/null
hmount "$IMG" >/dev/null
hmkdir ":System Folder"
for f in "$TMP"/sys*.bin; do hcopy -m "$f" ":System Folder:"; done
hattrib -b ":System Folder"

for app in "$HERE"/apps/*/build/*.bin; do
  [ -f "$app" ] || continue
  case $(basename "$app") in *.code.bin) continue ;; esac
  hcopy -m "$app" ":"
done

hmkdir ":$LABEL Files"
for kb in 10 100 1000; do
  name="$LABEL ${kb}K"
  python3 - "$TMP/t" "$LABEL" "$name" "$kb" <<'EOF'
import sys
out, vol, name, kb = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
lines, n = [], 0
while sum(len(l) for l in lines) < kb * 1024:
    n += 1
    lines.append(f"{vol} / {name} / line {n:06d} ABCDEFGHIJKLMNOPQRSTUVWXYZ\r")
open(out, "w").write("".join(lines))
EOF
  hcopy -r "$TMP/t" ":$LABEL Files:$name"
  hattrib -t TEXT -c ttxt ":$LABEL Files:$name"
done
printf '%s\r\rA bootable System 5.1 HD20 for fujinet-nio.\rCopy the files in "%s Files" to another FujiNet disk and compare: every line names its volume, file and line number.\r' "$LABEL" "$LABEL" > "$TMP/readme"
hcopy -r "$TMP/readme" ":Read Me"
hattrib -t TEXT -c ttxt ":Read Me"
hls -l
hls -l ":$LABEL Files"
hvol | tail -1
