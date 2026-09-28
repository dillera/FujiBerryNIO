#!/bin/bash
# Build run/boot608-config.dsk: a copy of the System 6.0.8 boot floppy
# (run/boot608.dsk, not committed) with the FujiNet CONFIG desk accessory
# (apps/fnconfig, FujiConfigNIO.flt) installed in its System file.
#   tools/make-config-floppy.sh [source.dsk] [output.dsk]
# Needs Retro68's hfsutils (hmount, hcopy, ...) on PATH or in
# $HOME/code/Retro68-build/toolchain/bin.
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
export PATH=$HOME/code/Retro68-build/toolchain/bin:$PATH
SRC=${1:-$HERE/run/boot608.dsk}
OUT=${2:-$HERE/run/boot608-config.dsk}
DA=${DA:-$HERE/apps/fnconfig/build/FujiConfigNIO.flt}
TMP=$(mktemp -d)
trap 'humount >/dev/null 2>&1 || true; rm -rf "$TMP"' EXIT

[ -f "$DA" ] || { echo "build apps/fnconfig first ($DA missing)" >&2; exit 1; }
cp "$SRC" "$OUT"
hmount "$OUT" >/dev/null
hcopy -m ':System Folder:System' "$TMP/System.bin"
# The older fujinet-mac-da DA ("FujiConfig", mock/serial transport) may be
# on the source floppy: drop it so the Apple menu has one CONFIG.
python3 "$HERE/tools/install_da.py" "$TMP/System.bin" "$DA" "$TMP/System-da.bin" \
    --name "FujiNet CONFIG" --id 21 --drop FujiConfig
hdel ':System Folder:System'
hcopy -m "$TMP/System-da.bin" ':System Folder:System'
hls -l ':System Folder'
humount >/dev/null
echo "Built $OUT"
