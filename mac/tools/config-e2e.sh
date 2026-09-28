#!/bin/bash
# FujiNet CONFIG desk accessory, headless in Snow: start fujinet-nio with
# only its boot HD20 (runtime mounts reset), boot System 6.0.8 from
# run/boot608-config.dsk (tools/make-config-floppy.sh), open FujiNet CONFIG
# from the Apple menu, browse host:/mac/, mount Floppy800.dsk in the floppy
# slot, then eject it. Screenshots: run/config-*.png.
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
FNRUN=${FNRUN:-$HOME/code/snow/target/release/fnrun}
cd "$HERE"

pkill -f "mac-floppy-tcp-debug/fujinet-nio" 2>/dev/null && sleep 1 || true
printf 'v1\n' > run/fujinet-data/fujinet-runtime-mounts.tsv
[ -f run/boot608-config.dsk ] || tools/make-config-floppy.sh >/dev/null
tools/run-nio.sh > run/nio-config.log 2>&1 &
NIO_PID=$!
trap 'kill $NIO_PID 2>/dev/null || true' EXIT
sleep 2

# double-click: two timed clicks (fnrun's dclick lands in one VBL)
DC=$'down\nrun 0.05\nup\nrun 0.15\ndown\nrun 0.05\nup'
cat > run/config.fnrun <<SCRIPT
rom MacPlus-v3.rom
floppy 0 boot608-config.dsk
run 40
# Apple menu > FujiNet CONFIG
move 15 9
down
run 0.5
move 60 171
run 0.5
up
run 15
shot config-1-hosts.png
# double-click host 1 (host:/mac/)
move 150 71
$DC
run 10
shot config-2-browse.png
# double-click Floppy800.dsk, then pick the FD slot
move 150 83
$DC
run 5
move 150 157
down
run 0.05
up
run 3
shot config-3-slot.png
# double-click FD: mount
$DC
run 25
shot config-4-mounted.png
# select the FD row, E ejects
move 150 217
down
run 0.05
up
run 2
type e
run 20
shot config-5-ejected.png
SCRIPT
(cd run && SNOW_FUJINET_DCD=127.0.0.1:65510 "$FNRUN" config.fnrun 2>&1 | grep '^shot' || true)
grep -c "fujibus: receive" run/nio-config.log | xargs echo "FujiBus requests from the Mac:"
