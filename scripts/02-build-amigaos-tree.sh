#!/usr/bin/env bash
# Build an expanded AmigaOS tree in the layout the fujinet-nio workspace expects
# for AMIBERRY_ASSET_ROOT.
#
# The workspace's amiberry-testing.md targets licensed AmigaOS 3.2 and wants:
#
#   $AMIBERRY_ASSET_ROOT/ROM/kickCDTVa1000a500a2000a600.rom
#   $AMIBERRY_ASSET_ROOT/ADF/Workbench3.2.adf
#   $AMIBERRY_ASSET_ROOT/L/FastFileSystem
#   $AMIBERRY_ASSET_ROOT/{C,S,Libs,Devs,System,...}   <- expanded OS tree
#
# We reproduce that layout from an AmigaOS 3.1 (Workbench 3.1 rev 40.42) six
# disk ADF set plus a Kickstart 3.1 ROM, because 3.2 is a paid Hyperion product.
# scripts/build-amiga-test-disk in the workspace packs this tree into the HDF,
# skipping the ROM/ and ADF/ directories, so keeping them inside the root is
# both intended and harmless.
#
# Everything here is user-supplied licensed data.  Nothing is downloaded and
# nothing is committed to this repository; only the paths are configurable.
set -euo pipefail

PROJ="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

# Where the six Workbench 3.1 ADFs live.  TOSEC keeps each image in its own
# directory whose basename repeats the image name.
TOSEC_ROOT="${TOSEC_ROOT:-$HOME/Downloads/_older_2026/Commodore Amiga - Operating Systems - Workbench (TOSEC-v2014-02-03)}"
WB_SET="${WB_SET:-Workbench v3.1 rev 40.42 (1994)(Commodore)(M10)}"

# Kickstart 3.1 40.63 (A600) is the ECS/68000 ROM, which matches the 68000 CPU
# the workspace profiles emulate.  40.68 (A1200) is AGA/68020-oriented.
KICKSTART_SRC="${KICKSTART_SRC:-$HOME/Downloads/_older_2026/RetroArch/.retroarch/system/kick40063.A600}"

OUT="${OUT:-$PROJ/assets/amigaOS3.1}"

XDFTOOL=(uvx --from amitools xdftool)

adf_for() { # $1 = "Disk 2 of 6)(Workbench)[!]"
  local n="$WB_SET($1"
  printf '%s/%s/%s.adf' "$TOSEC_ROOT" "$n" "$n"
}

unpack_disk() { # $1 = disk spec, $2 = destination
  local adf; adf="$(adf_for "$1")"
  [ -f "$adf" ] || { echo "missing ADF: $adf" >&2; return 1; }
  # xdftool writes the volume contents directly into the destination when it
  # does not exist yet.  If the destination already exists it instead creates
  # a <VolumeName>/ subdirectory plus .blkdev/.bootcode/.xdfmeta sidecars, so
  # make sure only the parent is present.
  mkdir -p "$(dirname "$2")"
  rm -rf "$2"
  "${XDFTOOL[@]}" "$adf" unpack "$2" >/dev/null
}

echo "==> output tree: $OUT"
rm -rf "$OUT"
mkdir -p "$OUT"

TMP="$(mktemp -d -t amigaos31)"
trap 'rm -rf "$TMP"' EXIT

# Mirror what the AmigaOS 3.1 HD installer does: Workbench and Extras land on
# the system volume root, the remaining disks become their own directories.
echo "==> Disk 2 (Workbench) -> /"
unpack_disk "Disk 2 of 6)(Workbench)[!]" "$TMP/workbench"
cp -R "$TMP/workbench/." "$OUT/"

echo "==> Disk 3 (Extras) -> /"
unpack_disk "Disk 3 of 6)(Extras)[!]" "$TMP/extras"
cp -R "$TMP/extras/." "$OUT/"

echo "==> Disk 4 (Storage) -> /Storage"
unpack_disk "Disk 4 of 6)(Storage)[!]" "$OUT/Storage"

echo "==> Disk 5 (Locale) -> /Locale"
unpack_disk "Disk 5 of 6)(Locale)[!]" "$OUT/Locale"

echo "==> Disk 6 (Fonts) -> /Fonts"
unpack_disk "Disk 6 of 6)(Fonts)[!]" "$OUT/Fonts"

# The Workbench floppy ships a ROM-overlay FastFileSystem; the Install disk
# carries the full one that a hard-disk install uses.
echo "==> Disk 1 (Install) -> L/FastFileSystem"
unpack_disk "Disk 1 of 6)(Install)[!]" "$TMP/install"
mkdir -p "$OUT/L"
cp "$TMP/install/L/FastFileSystem" "$OUT/L/FastFileSystem"

# Unpacking Storage/Locale/Fonts leaves xdftool bookkeeping files beside each
# created directory.  They are host-side metadata, not Amiga files, and would
# otherwise be packed into the HDF.
find "$OUT" -maxdepth 1 \( -name '*.blkdev' -o -name '*.bootcode' -o -name '*.xdfmeta' \) -delete

# ROM/ and ADF/ mirror the 3.2 asset-root convention.  build-amiga-test-disk
# excludes both when packing, and run.py reads them by path.
echo "==> ROM/ and ADF/"
mkdir -p "$OUT/ROM" "$OUT/ADF"
cp "$KICKSTART_SRC" "$OUT/ROM/kick31.rom"
cp "$(adf_for "Disk 2 of 6)(Workbench)[!]")" "$OUT/ADF/Workbench3.1.adf"

# The 3.1 Startup-Sequence assumes a floppy boot: it adds df0: buffers and
# leaves ENV: unassigned until IPrefs runs.  Booting the same script from a
# hardfile works, but the df0: line costs several seconds of failed access on
# a machine with no floppy inserted.
if grep -q "AddBuffers" "$OUT/S/Startup-Sequence" 2>/dev/null; then
  echo "==> trimming floppy-only AddBuffers from S/Startup-Sequence"
  # Keep the file latin-1; AmigaDOS scripts are not UTF-8.
  python3 - "$OUT/S/Startup-Sequence" <<'PY'
import sys
from pathlib import Path
p = Path(sys.argv[1])
text = p.read_text(encoding="latin-1")
lines = [l for l in text.splitlines(True) if "AddBuffers" not in l]
p.write_text("".join(lines), encoding="latin-1")
PY
fi

echo
echo "==> built $OUT"
echo "    kickstart : $OUT/ROM/kick31.rom ($(stat -f%z "$OUT/ROM/kick31.rom") bytes)"
echo "    boot ADF  : $OUT/ADF/Workbench3.1.adf"
echo "    FFS       : $OUT/L/FastFileSystem"
echo "    serial    : $(test -f "$OUT/Devs/serial.device" && echo present || echo MISSING)"
echo "    LoadWB    : $(grep -c LoadWB "$OUT/S/Startup-Sequence" || true) reference(s)"
