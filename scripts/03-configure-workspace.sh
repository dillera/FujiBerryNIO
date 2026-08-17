#!/usr/bin/env bash
# Apply this Mac's configuration to the cloned fujinet-nio-workspace:
#   - local/config.env       (toolchain, Amiberry binary, AmigaOS 3.1 assets)
#   - a wb3.1 profile in configs/amiga/workbenches.yaml
#
# Safe to re-run; both edits are idempotent.
set -euo pipefail

PROJ="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
WS="${WS:-$PROJ/workspace}"

[ -d "$WS" ] || { echo "workspace not found: $WS" >&2; exit 1; }

# --- local/config.env ------------------------------------------------------
mkdir -p "$WS/local"
sed "s|__PROJ__|$PROJ|g" "$PROJ/config/config.env" > "$WS/local/config.env"
echo "==> wrote $WS/local/config.env"

# --- wb3.1 profile ---------------------------------------------------------
# Mirrors the upstream wb3.2 profile (build_test_disk, 68000, 512K chip,
# 8MB fast) but names the 3.1 Kickstart this asset root actually ships.
PROFILES="$WS/configs/amiga/workbenches.yaml"
if grep -q "^  wb3.1:" "$PROFILES"; then
  echo "==> wb3.1 profile already present in $PROFILES"
else
  python3 - "$PROFILES" <<'PY'
import sys
from pathlib import Path

path = Path(sys.argv[1])
text = path.read_text()

block = """  wb3.1:
    build_test_disk: true
    kickstart: ${AMIBERRY_ASSET_ROOT}/ROM/kick31.rom
    settings:
      cpu_type: 68000
      chipmem_size: 512
      fastmem_size: 8
      cpu_compatible: true
      cachesize: 0

"""

# Insert directly after the "profiles:" header so it sits beside wb3.2.
marker = "profiles:\n"
at = text.index(marker) + len(marker)
path.write_text(text[:at] + "\n" + block + text[at:].lstrip("\n"))
print("inserted wb3.1 profile")
PY
  echo "==> patched $PROFILES"
fi

echo
echo "==> verifying"
cd "$WS"
# shellcheck source=/dev/null
source scripts/env.sh
echo "    AMIGA_TOOLCHAIN_BIN = $AMIGA_TOOLCHAIN_BIN"
echo "    AMIBERRY_BIN        = $AMIBERRY_BIN"
echo "    AMIBERRY_ASSET_ROOT = $AMIBERRY_ASSET_ROOT"
echo "    AMIBERRY_KICKSTART  = $AMIBERRY_KICKSTART"
for f in "$AMIBERRY_BIN" "$AMIBERRY_KICKSTART" "$AMIBERRY_WORKBENCH_ADF" "$AMIBERRY_FAST_FILE_SYSTEM"; do
  [ -e "$f" ] && echo "    ok   $f" || echo "    MISS $f"
done
