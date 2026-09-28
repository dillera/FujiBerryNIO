#!/bin/bash
# Launch Snow (built from the fujinet-dcd branch) as a Mac Plus whose
# external floppy port carries a FujiNet DCD chain served by fujinet-nio.
# Start tools/run-nio.sh first.
#
#   SNOW_SRC   Snow checkout, fujinet-dcd branch (default ~/code/snow)
#   ROM        Mac Plus ROM (default run/MacPlus-v3.rom)
#   FLOPPY     boot floppy  (default run/boot608.dsk, System 6.0.8)
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
SNOW_SRC=${SNOW_SRC:-$HOME/code/snow}
SNOW=$SNOW_SRC/target/release/snowemu
ROM=${ROM:-$HERE/run/MacPlus-v3.rom}
FLOPPY=${FLOPPY:-$HERE/run/boot608.dsk}

[ -x "$SNOW" ] || (cd "$SNOW_SRC" && cargo build --release -p snow_frontend_egui)
cat > "$HERE/run/fujinet-mac.snoww" <<JSON
{
  "rom_path": "$ROM",
  "model": "Plus",
  "floppy_images": [ "$FLOPPY" ],
  "viewport_scale": 2.0
}
JSON
export SNOW_FUJINET_DCD=${SNOW_FUJINET_DCD:-127.0.0.1:65510}
exec "$SNOW" "$HERE/run/fujinet-mac.snoww" "$@"
