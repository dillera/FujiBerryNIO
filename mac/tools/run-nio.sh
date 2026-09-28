#!/bin/bash
# Run fujinet-nio's Mac floppy-port bus (mac-floppy-tcp-debug) in mac/run.
# It listens on 127.0.0.1:65510 (fujinet-data/fujinet.yaml) for the drive
# side: Snow with SNOW_FUJINET_DCD=127.0.0.1:65510, or tools/pico_sim.py.
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
NIO=${NIO:-$HERE/../workspace/repos/fujinet-nio/build/mac-floppy-tcp-debug/fujinet-nio}
cd "$HERE/run"
exec "$NIO" "$@"
