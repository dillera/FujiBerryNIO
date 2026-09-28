# FujiNet NIO on the classic Macintosh (floppy port)

fujinet-nio serving a 68000 Mac through its **floppy port**: HD20 (DCD) disks,
and **FujiBus commands from Mac programs carried over the IWM**. The
classic FujiNet Mac firmware has the first; nothing had the second.

Verified in the Snow emulator, as a Mac Plus (ROM v3) running System 6.0.8:

* NIO's `FujiNet.hda` mounts on the desktop as an HD20 with the FujiNet icon.
* The Finder opens it, and runs an application from it.
* The Finder duplicates a 400 KB file on it. The copy is byte-identical: that
  exercises 800 blocks of HD20 reads and writes.
* **FujiNetProbe**, a Mac app built on fujinet-nio-lib, finds the FujiNet and
  reads its clock. It then has the FujiNet make an HTTP GET (live weather,
  371 bytes). Every call is a FujiBus packet through the floppy port.

![FujiNetProbe on a Mac Plus](evidence/probe-clock-http.png)

## How it fits together

```
Mac (68000, ROM HD20 driver in .Sony)          real hardware: DB-19 floppy port
   |  IWM: DCD phases, !HSHK, 7-to-8 groups     -> FujiNet Mac board's Pico
   v
Snow  core/src/mac/swim/dcd.rs                  plays the Pico's part
   |  Pico <-> FujiNet protocol over TCP        -> on the board: 2 Mbaud UART
   v   ('A'..'D' select, 'R' read, 'W' write, 'T' status, 'h' units)
fujinet-nio  MacFloppyFramer                    (patch 0006)
   |-- HD20 blocks  -> DiskService (slot n = DCD unit n)
   `-- mailbox blocks -> FujiBus -> clock, network, disk, ... devices
```

The drive side of the link is exactly what the Pico on the FujiNet Mac board
already speaks to the ESP32 (`fujinet-firmware lib/bus/mac/mac.h`). So the
same NIO framer should serve the real board, unchanged, once NIO runs on its
ESP32 with that UART as the channel.

### FujiBus over the floppy port: the mailbox

Every HD20 unit reports 16 more blocks than its image holds. Those blocks
never reach the image:

* **Write** blocks starting at the first of them: one raw FujiBus packet.
* **Read** them: the response.

```
block 0, bytes 0..15:  "FNIO"  version(1)  state  seq(u16)  length(u32)  reserved
                       state: 0 idle, 1 response ready, 2 request pending
bytes 16..             the packet, continuing into blocks 1..15 (8176 bytes max)
```

The Mac needs no driver, INIT or patch. A program finds the mailbox by
walking the drive queue and reading the block at `drive size - 16`, looking
for `FNIO`. It then does ordinary driver-level `PBWrite`/`PBRead` through
the ROM's HD20 support. The board's Pico needs no change either: to it these
are ordinary block reads and writes. The volume's own blocks are untouched,
so the unit is still a normal HFS disk.

marciot's `mac68k-fujinet-serial` pioneered data over DCD block I/O, using a
knock sequence and a magic file on the volume. The mailbox past the end of
the volume needs neither.

## Pieces

| Where | What |
| --- | --- |
| `patches/0006-fujinet-nio-mac-floppy-bus.patch` | NIO: `MacFloppyFramer`, profile `Mac68k`/`MacFloppy`, preset `mac-floppy-tcp-debug`, `.hda/.hfv/.dsk` as 512-byte-block images |
| `patches/0007-fujinet-nio-lib-mac68k.patch` | fujinet-nio-lib: `mac68k` target (Retro68), `src/platform/mac68k/fn_transport.c` |
| `mac/snow/0001-snow-fujinet-dcd-chain.patch` | Snow: the DCD chain (`dcd.rs`) and `fnrun`, a scripted headless runner. Also committed on branch `fujinet-dcd` of `~/code/snow` |
| `mac/apps/fnprobe` | FujiNetProbe: clock + HTTP GET through fujinet-nio-lib, a 54 KB Toolbox app |
| `mac/tools/run-nio.sh` | Run NIO's Mac bus on `127.0.0.1:65510` (config in `run/fujinet-data/fujinet.yaml`) |
| `mac/tools/run-snow.sh` | Snow GUI as a Mac Plus with the DCD chain (`SNOW_FUJINET_DCD`) |
| `mac/tools/make-fujinet-volume.sh` | Build the `FujiNet` HD20 volume with the apps (NIO stopped) |
| `mac/tools/pico_sim.py` | Plays the Pico against NIO: units, status, HFS blocks, a FujiBus clock call through the mailbox |
| `mac/tools/e2e.sh` | The whole thing headless, with screenshots |
| `mac/evidence/` | Screenshots from the verified runs |

## Running it

Needs: the workspace NIO checkout with patch 0006 applied, a Retro68 toolchain
(`~/code/Retro68-build`), Rust, a Snow checkout on the `fujinet-dcd` branch
(`~/code/snow`), and in `mac/run/` (not committed, Apple software):
`MacPlus-v3.rom` and a System 6.0.8 boot floppy `boot608.dsk`.

```sh
# NIO, Mac floppy-port profile
cd workspace/repos/fujinet-nio
cmake --preset mac-floppy-tcp-debug && cmake --build build/mac-floppy-tcp-debug

# fujinet-nio-lib for the Mac, and the probe app
cd ../fujinet-nio-lib && make mac68k
cd ../../../mac/apps/fnprobe
cmake -B build -DCMAKE_TOOLCHAIN_FILE=$HOME/code/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
cmake --build build

# volume, NIO, Mac
cd ../..
tools/make-fujinet-volume.sh
tools/run-nio.sh &
tools/run-snow.sh          # interactive; or tools/e2e.sh headless
```

In Snow the FujiNet disk appears under the boot floppy. Open it and run
**FujiNetProbe**.

`run/fujinet-data/fujinet.yaml` mounts `host:/mac/FujiNet.hda` read/write as
unit 0 (the boot config mount). Stop NIO before changing an image on the
host: NIO keeps it open, and the Mac would be served stale blocks and write
a stale catalog back.

## Notes and limits

* **HD20 size.** Keep images at or below 65,519 blocks (32 MB less the
  mailbox). The Mac rejects HD20s over 65,535 blocks.
* **Floppy (MCI).** Not yet served by NIO. In Snow the Mac's own floppy
  drives work as usual. On the real board the floppy is streamed as GCR from
  the ESP32's RMT peripheral, which NIO does not have. The framer ignores the
  board's floppy command bytes, so the link stays in step.
* **Flushing.** The Mac caches the catalog until unmount or Shut Down.
  Killing the emulator loses the Finder's last changes, as on real hardware.
* **Snow warnings.** `IWM unknown read q6 = true q7 = true` during HD20
  writes is Snow noting a register state it does not model; harmless.
* **Retro68 console apps** (the `CONSOLE` flag) pull in libstdc++ iostreams:
  900 KB, too large to run here. FujiNetProbe draws into a plain window.

## Next

* Floppy images served by NIO: port the GCR encoder (`fujinet-firmware
  lib/media/mac/macGCR.cpp`), and add a track-data message to the board
  protocol for emulators.
* NIO on the FujiNet Mac board's ESP32 with the Pico UART as the channel.
* Mac-side CONFIG over the mailbox: host and slot browsing, mounting (the
  `fn_disk_*` and slot-catalog calls already work through it).
