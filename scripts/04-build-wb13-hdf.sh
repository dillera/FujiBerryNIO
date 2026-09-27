#!/usr/bin/env bash
# Build the clean, bootable Workbench 1.3 HDF that the workspace's `wb13`
# environment and `wb13-a500` profile expect (AMIGA_WB13_HDF), then build the
# disposable E2E base from it.
#
# Upstream treats this image as a "manually installed" HDF and never creates
# it.  A WB1.3 hard-disk install is just the Workbench floppy's contents on an
# FFS volume, so we do that non-interactively:
#
#   1. unpack the WB1.3 Workbench ADF with its xdftool metadata sidecars, so
#      protection bits (script `s`, pure `p`) and the boot block survive;
#   2. drop the floppy-only `Addbuffers df0:` line from S/Startup-Sequence
#      (same fix upstream applies to its wb31/wb32 recipes);
#   3. pack it as a 20MB FFS hardfile, like an A590 of the period.
#
# Kickstart 1.3 has no FastFileSystem in ROM, so Amiberry loads the WB1.3
# L/FastFileSystem from the host.  The runner looks for it beside the built
# env base HDF, but the `prebuilt_hdf` builder only copies the HDF itself, so
# we place it there after `amiga-env build`.
#
# Inputs come from <workspace>/local/amiga.env:
#   AMIGA_WB13_ADF_WORKBENCH   Workbench 1.3 disk 1 (1.3.3 rev 34.34 tested)
#   AMIGA_WB13_HDF             output path for the clean HDF
set -euo pipefail

PROJ="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
WS="${WS:-$PROJ/workspace}"
MACHINE="${MACHINE:-a500-000}"
SIZE="${SIZE:-20M}"
XDFTOOL=(uvx --from amitools xdftool)

# shellcheck source=/dev/null
source "$WS/local/amiga.env"
: "${AMIGA_WB13_ADF_WORKBENCH:?set in $WS/local/amiga.env}"
: "${AMIGA_WB13_HDF:?set in $WS/local/amiga.env}"
[ -f "$AMIGA_WB13_ADF_WORKBENCH" ] || { echo "missing ADF: $AMIGA_WB13_ADF_WORKBENCH" >&2; exit 1; }

TMP="$(mktemp -d -t wb13hdf)"
trap 'rm -rf "$TMP"' EXIT

# Unpacking into an existing directory makes xdftool write <Volume>/ plus the
# .xdfmeta/.bootcode sidecars that `pack` reads back.
echo "==> unpacking $(basename "$AMIGA_WB13_ADF_WORKBENCH")"
"${XDFTOOL[@]}" "$AMIGA_WB13_ADF_WORKBENCH" unpack "$TMP" >/dev/null
VOL="$(find "$TMP" -mindepth 1 -maxdepth 1 -type d | head -1)"
[ -f "$VOL/L/FastFileSystem" ] || [ -f "$VOL/l/FastFileSystem" ] || {
  echo "ADF has no L/FastFileSystem: not a WB1.3 Workbench disk?" >&2; exit 1; }
SS="$(find "$VOL" -ipath "$VOL/s/startup-sequence" | head -1)"
[ -n "$SS" ] || { echo "ADF has no S/Startup-Sequence" >&2; exit 1; }

echo "==> dropping floppy-only Addbuffers from S/Startup-Sequence"
python3 - "$SS" <<'PY'
import sys
from pathlib import Path
p = Path(sys.argv[1])
text = p.read_text(encoding="latin-1")
lines = [l for l in text.splitlines(True) if not l.lower().lstrip().startswith("addbuffers")]
p.write_text("".join(lines), encoding="latin-1")
PY

echo "==> packing $SIZE FFS hardfile -> $AMIGA_WB13_HDF"
mkdir -p "$(dirname "$AMIGA_WB13_HDF")"
rm -f "$AMIGA_WB13_HDF"
"${XDFTOOL[@]}" "$AMIGA_WB13_HDF" pack "$VOL" ffs "size=$SIZE" >/dev/null
FFS="$(dirname "$AMIGA_WB13_HDF")/FastFileSystem"
cp "$(find "$VOL" -ipath "$VOL/l/fastfilesystem" | head -1)" "$FFS"
"${XDFTOOL[@]}" "$AMIGA_WB13_HDF" boot show | grep -E "dos_type|bootable"

echo "==> building workspace env wb13/$MACHINE"
cd "$WS"
scripts/amiga-env build wb13 --machine "$MACHINE" --force
cp "$FFS" "build/amiga-envs/wb13/$MACHINE/FastFileSystem"

# The interactive wb13-a500 profile boots a persistent, user-owned image.
# Seed it once from the clean HDF; never overwrite an existing one.
RUN_HDF="$WS/images/amigaos1.3-run.hdf"
if [ ! -e "$RUN_HDF" ]; then
  mkdir -p "$(dirname "$RUN_HDF")"
  cp "$AMIGA_WB13_HDF" "$RUN_HDF"
  cp "$FFS" "$(dirname "$RUN_HDF")/FastFileSystem"
  echo "==> seeded interactive image $RUN_HDF"
fi

echo
echo "==> done"
echo "    clean HDF : $AMIGA_WB13_HDF"
echo "    E2E base  : $WS/build/amiga-envs/wb13/$MACHINE/base.hdf"
