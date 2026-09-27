#!/usr/bin/env bash
# Boot an interactive Workbench in Amiberry with FujiNet NIO attached.
#
# Thin wrapper around the workspace target:
#
#   scripts/build.sh amiga-workbench --profile <profile> [-- <runner args>]
#
# which builds the profile's versioned artifact package (wb13 = nix13 CRT,
# wb31 = clib2), mounts it read-only as NIO:, starts the POSIX FujiNet NIO,
# and bridges Amiberry's serial port to it with socat.
#
# Usage:
#   scripts/05-run.sh                        # Workbench 1.3, A500/68000
#   PROFILE=wb31-a1200 scripts/05-run.sh     # Workbench 3.1, A1200/68030
#   scripts/05-run.sh -- --external-nio      # attach to an NIO you started
#
# In WB1.3, install the drivers permanently with:
#   Execute NIO:Install-FujiNet-WB13
set -euo pipefail

PROJ="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
WS="${WS:-$PROJ/workspace}"
PROFILE="${PROFILE:-wb13-a500}"

cd "$WS"
# shellcheck source=/dev/null
source scripts/env.sh

# Kickstart 1.3 has no FastFileSystem in ROM, and the wb13-a500 profile names
# none, so point Amiberry at the WB1.3 copy that 04-build-wb13-hdf.sh seeded.
if [ "$PROFILE" = wb13-a500 ] && [ -z "${AMIBERRY_FAST_FILE_SYSTEM:-}" ]; then
  export AMIBERRY_FAST_FILE_SYSTEM="$WS/images/FastFileSystem"
fi

echo "==> profile   : $PROFILE"
echo "==> amiberry  : $AMIBERRY_BIN"
echo "==> toolchain : $(command -v m68k-amigaos-gcc || echo 'NOT FOUND')"

set -x
exec env -u AMIGA_TEST_COMMAND -u AMIGA_TEST_APP \
  ./scripts/build.sh amiga-workbench --profile "$PROFILE" "$@"
