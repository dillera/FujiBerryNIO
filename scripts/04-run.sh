#!/usr/bin/env bash
# Build the Amiga apps + interactive HDF and boot it in Amiberry with FujiNet
# NIO attached.
#
# This is a thin wrapper around the workspace's own target:
#
#   scripts/build.sh amiga-workbench --profile wb3.1 [-- <runner args>]
#
# which builds fujinet-nio-lib for Amiga, the nio-apps/nio-core-apps
# executables, packs them into a bootable HDF alongside the expanded AmigaOS
# 3.1 tree, then launches Amiberry with its emulated serial.device bridged by
# socat to the POSIX FujiNet NIO TCP channel.
#
# Usage:
#   scripts/04-run.sh                 # runner starts its own FujiNet NIO
#   scripts/04-run.sh --external-nio  # attach to an NIO you started yourself
#   scripts/04-run.sh --all-apps      # install every Amiga app in the image
#
# Note: --with-driver is intentionally NOT supported on the 3.1 profile.  It
# adds `C:LoadModule DEVS:fujinet-disk.device` to the startup sequence, and
# LoadModule is an AmigaOS 3.2 command that does not exist in 3.1.
set -euo pipefail

PROJ="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
WS="${WS:-$PROJ/workspace}"
PROFILE="${PROFILE:-wb3.1}"

# Split our args into build-side (before --) and runner-side (after --).
build_args=()
runner_args=()
seen_sep=0
for a in "$@"; do
  if [ "$a" = "--" ]; then seen_sep=1; continue; fi
  if [ "$seen_sep" -eq 1 ]; then runner_args+=("$a")
  elif [ "$a" = "--external-nio" ]; then runner_args+=("$a")
  else build_args+=("$a"); fi
done

for a in ${build_args[@]+"${build_args[@]}"}; do
  if [ "$a" = "--with-driver" ]; then
    echo "refusing --with-driver: LoadModule is AmigaOS 3.2 only, this is a 3.1 image" >&2
    exit 2
  fi
done

cd "$WS"
# shellcheck source=/dev/null
source scripts/env.sh

echo "==> profile     : $PROFILE"
echo "==> amiberry    : $AMIBERRY_BIN"
echo "==> kickstart   : $AMIBERRY_KICKSTART"
echo "==> asset root  : $AMIBERRY_ASSET_ROOT"
echo "==> toolchain   : $(command -v m68k-amigaos-gcc || echo 'NOT FOUND')"
echo

set -x
exec ./scripts/build.sh amiga-workbench --profile "$PROFILE" \
  ${build_args[@]+"${build_args[@]}"} \
  ${runner_args[0]+-- "${runner_args[@]}"}
