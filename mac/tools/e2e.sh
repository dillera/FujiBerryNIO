#!/bin/bash
# End-to-end test, headless: rebuild the FujiNet volume with the apps,
# start fujinet-nio, boot a Mac Plus in Snow with the DCD chain, open the
# FujiNet HD20 volume, run FujiNetProbe (clock + HTTP over FujiBus through
# the floppy port) and save screenshots in run/e2e-*.png.
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
FNRUN=${FNRUN:-$HOME/code/snow/target/release/fnrun}
cd "$HERE"

pkill -f "mac-floppy-tcp-debug/fujinet-nio" 2>/dev/null && sleep 1 || true
tools/make-fujinet-volume.sh >/dev/null
tools/run-nio.sh > run/nio.log 2>&1 &
NIO_PID=$!
trap 'kill $NIO_PID 2>/dev/null || true' EXIT
sleep 1.5
python3 tools/pico_sim.py

cat > run/e2e.fnrun <<'SCRIPT'
rom MacPlus-v3.rom
floppy 0 boot608.dsk
run 40
shot e2e-1-desktop.png
# select the FujiNet HD20 volume and File > Open
move 473 105
run 0.5
click
run 1
key 1f cmd
run 15
shot e2e-2-volume.png
# FujiNetProbe, second icon in the window (FujiNetDisks is first)
move 193 145
run 0.5
click
run 1
key 1f cmd
run 30
shot e2e-3-probe.png
# quit to the Finder: the floppy the probe mounted is in the external drive
click
run 20
shot e2e-4-floppy.png
SCRIPT
(cd run && SNOW_FUJINET_DCD=127.0.0.1:65510 "$FNRUN" e2e.fnrun)
grep -c "fujibus: receive" run/nio.log | xargs echo "FujiBus requests from the Mac:"
