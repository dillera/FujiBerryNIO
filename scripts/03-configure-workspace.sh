#!/usr/bin/env bash
# Configure the cloned workspace for this Mac and build the WB3.1 environment:
#   - local/config.env  (toolchain, Amiberry binary)
#   - local/amiga.env   (licensed media paths; see config/amiga.env)
#   - build/amiga-envs/wb31/base.hdf via upstream's six-disk builder
#   - images/amigaos3.1-run.hdf for the interactive wb31-a1200 profile
#
# Upstream's scripts/amiga-env now assembles the 3.1 HDF from the six ADFs
# itself, replacing this repo's former expanded-tree step and wb3.1 profile.
#
# Media locations (override as needed):
#   TOSEC_ROOT  TOSEC "Operating Systems - Workbench" folder
#   KICK_DIR    directory with kick40063.A600 and kick34005.A500
set -euo pipefail

PROJ="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
WS="${WS:-$PROJ/workspace}"
TOSEC_ROOT="${TOSEC_ROOT:-$HOME/Downloads/_older_2026/Commodore Amiga - Operating Systems - Workbench (TOSEC-v2014-02-03)}"
KICK_DIR="${KICK_DIR:-$HOME/Downloads/_older_2026/RetroArch/.retroarch/system}"

[ -d "$WS" ] || { echo "workspace not found: $WS (run 02-clone-workspace.sh)" >&2; exit 1; }

mkdir -p "$WS/local"
cp "$PROJ/config/config.env" "$WS/local/config.env"
echo "==> wrote $WS/local/config.env"

# '|' is safe as the sed delimiter: none of these paths contain it.
sed -e "s|__TOSEC__|$TOSEC_ROOT|g" -e "s|__KICK__|$KICK_DIR|g" -e "s|__PROJ__|$PROJ|g" \
  "$PROJ/config/amiga.env" > "$WS/local/amiga.env"
echo "==> wrote $WS/local/amiga.env"

echo "==> checking media"
missing=0
while IFS= read -r path; do
  case "$path" in *wb13-clean.hdf) continue ;; esac  # built by step 04
  if [ -f "$path" ]; then echo "    ok   ${path##*/}"
  else echo "    MISS $path"; missing=1; fi
done < <(grep -v '^[[:space:]]*#' "$WS/local/amiga.env" | grep -o '"[^"]*"' | tr -d '"')
[ "$missing" -eq 0 ] || { echo "fix TOSEC_ROOT / KICK_DIR and re-run" >&2; exit 1; }

cd "$WS"
scripts/amiga-env build wb31

# The interactive profile boots a persistent, user-owned image.  Seed it once
# from the pristine base; never overwrite an existing one.
RUN_HDF="$WS/images/amigaos3.1-run.hdf"
if [ ! -e "$RUN_HDF" ]; then
  mkdir -p "$(dirname "$RUN_HDF")"
  cp build/amiga-envs/wb31/base.hdf "$RUN_HDF"
  echo "==> seeded interactive image $RUN_HDF"
fi
