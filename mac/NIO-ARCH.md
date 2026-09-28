# FujiNet NIO on the classic Macintosh: architecture

This is the system that, on 2026-09-28, booted a real 68000 Macintosh from a
disk image on a TNFS server. **fujinet-nio** ran on the ESP32 of the FujiNet
Mac 68k board and served the image through the Mac's floppy port. The same
code runs in the Snow emulator, where it also serves floppies, and it lets
Mac programs send FujiNet commands over the floppy port. No FujiNet firmware
could do that before.

It covers the hardware and emulated paths, every protocol layer, the NIO
code, the Mac client library and apps, the Snow changes, the build and flash
steps, the problems found on the way and how they were fixed, and what is
left.

---

## 1. The big picture

```
 Macintosh (68000, System 5.1 / 6.0.8)
   ROM .Sony driver: 800K floppies and HD20 (DCD) disks
   IWM floppy controller ----- DB-19 floppy port
                                   |
       real hardware               |                emulation
 ----------------------------------+----------------------------------------
  Raspberry Pi Pico (PIO)          |   Snow emulator, core/src/mac/swim/dcd.rs
  drive side of DCD and MCI,       |   drive side of DCD, bit-timed through
  unchanged classic firmware       |   the emulated IWM; floppy tracks from NIO
          |  2 Mbaud UART          |          |  TCP 127.0.0.1:65510
          v                        |          v
  ESP32-WROVER-E running           |   fujinet-nio POSIX build
  fujinet-nio (mac-floppy-         |   (mac-floppy-tcp-debug)
  fujimac-rev0)                    |
 ----------------------------------+----------------------------------------
                    MacFloppyFramer  (the FujiNet side of the Pico protocol)
                    |-- HD20 blocks  -> DiskService slots 1-4 -> image files
                    |                   (flash:, sd0:, host:, tnfs://)
                    |-- 800K floppy  -> DiskService slot 5, GCR encoder and
                    |                   decoder  (emulator link only, for now)
                    `-- mailbox blocks -> FujiBusTransport -> NIO devices
                                          (clock, network/HTTP, disk, file, ...)
```

There are two paths:

* **Real hardware.** The Pico on the board is untouched. It speaks the drive
  side of the Mac's disk protocols in PIO and, over a UART, a one-character
  protocol to the ESP32. The ESP32 now runs NIO instead of the classic
  firmware. NIO implements the ESP32's half of that protocol.
* **Emulation.** Snow plays the Pico's part and speaks the same protocol to
  a POSIX build of NIO over TCP.

Everything above the Pico protocol is shared: the framer, the disk images,
FujiBus, and the Mac software.

---

## 2. The Mac's side: what the Mac actually does

### 2.1 HD20 / Directly Connected Disks (DCD)

Apple's HD20 hard disk plugged into the floppy port. The protocol, called
DCD, is documented in lampmerchant's tashnotes (`macintosh/floppy/dcd`). The
128K ROMs (512Ke, Plus) and later carry the driver in `.Sony`. The protocol:

* **Phase lines as states.** CA2/CA1/CA0 select one of eight states.
  * States 2/3 put the device's **!HSHK** handshake on the RD line.
  * State 1 is a data transfer; state 0 is **holdoff** (the Mac pauses).
  * State 4 is reset.
  * States 5/6/7 let the ROM detect a DCD (RD reads low, high, high).
* **Handshake.** The Mac asserts HOST (state 2 to 3), and the device asserts
  !HSHK. The Mac goes to state 1 and sends `0xAA`, then the number of groups
  it will send and the number it expects back, then 7-to-8 groups. It goes
  back to state 3, the device releases !HSHK, and the Mac drops to state 2.
  The device then asserts !HSHK when its reply is ready, and the Mac reads it.
* **7-to-8 encoding.** The IWM needs bit 7 set in every byte. Each group of 7
  bytes is sent shifted right with the MSB set, plus one byte holding their
  LSBs. From the Mac the LSB byte comes **first**; from the device it comes
  **last**.
* **Commands.** Every payload ends with a checksum.

  | Command | What |
  | --- | --- |
  | `0x00` | Read N sectors (539-byte replies: 20 tag bytes + 512 data) |
  | `0x01`/`0x41` | Write, and write continuation |
  | `0x02`/`0x42` | Write and verify |
  | `0x03` | Status (343 bytes: size in blocks, flags, icon) |
  | `0x04` | ID |
  | `0x19` / `0x1A` | Format / verify |
* **Daisy chain.** A pulse on CA3 (LSTRB) passes !ENBL on to the next device.
  The floppy drive sits at the end of the chain.
* **Size limit.** The Mac rejects an HD20 of more than 65,535 blocks.

The ROM probes the chain **once, at startup**. An HD20 that appears later is
not seen until the Mac restarts.

### 2.2 The 800K floppy (MCI)

The Mac reads GCR-encoded tracks (`D5 AA 96` address fields, `D5 AA AD` data
fields, 6-and-2 coding, five speed zones) and writes a sector by rewriting
only its data field. The board's ESP32 streams tracks from its RMT
peripheral, and the Pico captures the Mac's WR line as a bitstream.

---

## 3. The board protocol (Pico <-> FujiNet)

The Pico on the board and the classic ESP32 firmware already spoke this
(`fujinet-firmware lib/bus/mac/mac.h`). NIO speaks the FujiNet side of it
unchanged, plus a few additions marked **new**.

| From the drive side (Pico / Snow) | Reply | |
| --- | --- | --- |
| `A`..`D` | none | select DCD unit 0..3 |
| `R` b2 b1 b0 | 512 bytes | read block (24-bit, big endian) |
| `W` b2 b1 b0 + 512 bytes | `w` or `e` | write block |
| `T` | 336 bytes | HD20 status block (payload offsets 6..341) |
| `0` / `4` | none | floppy step direction |
| `1` | track\|0x80 or `N` | floppy step |
| `2` / `6` | `M` / `F` | floppy motor on / off |
| `7` | `E` | the Mac ejected the floppy |
| `?` **new** | `h` n | how many DCD units |
| `!` **new** | none | polled mode (emulators) |
| `#` cyl side **new** | nbits, bits | one GCR track (emulators) |
| `P` cyl side nbits bits **new** | `p` n | a whole written track back (emulators) |

Unsolicited, from the FujiNet:
* `h` n: DCD units in the chain.
* `s`/`d` t: a 400K/800K floppy is in.
* `u`/`l`: writable/locked.
* `r`: the floppy was removed.

**Polled mode.** An unsolicited byte can land in front of a reply the drive
side is waiting for. The Pico copes by draining its UART before each command.
Snow sends `!` instead: NIO then never speaks unasked, and `?` returns
`h` n f, where f is the floppy state.

---

## 4. FujiBus over the floppy port: the mailbox

This is the part that is new.

**Idea.** Every HD20 unit reports **16 more blocks than its image holds**.
Those blocks never touch the image:

* **Writing** blocks starting at the first of them carries one raw
  (un-SLIPped) FujiBus packet from the Mac.
* **Reading** them returns the response.

```
mailbox block 0, bytes 0..15:
   "FNIO"  version=1  state  seq (u16 BE)  length (u32 BE)  reserved (4)
   state: 0 idle, 1 response ready, 2 request pending
bytes 16..: the packet, continuing into blocks 1..15 (8,176 bytes max)
```

**On the Mac.** No driver, INIT or patch is needed. fujinet-nio-lib's
`mac68k` transport does the whole job:
1. Walk the drive queue (low-memory `DrvQHdr` at `$308`).
2. For each drive, read block `size - 16` through the ROM `.Sony` driver
   (`PBReadSync` with the driver refnum and drive number) and look for
   `FNIO`.
3. For each request, write the header and packet (`PBWriteSync`), then poll
   block 0 until the state is 1 and the seq matches, then read the rest.

**On the Pico.** Nothing changes: these are ordinary block reads and writes.

**In NIO.** `MacFloppyFramer` sees writes to mailbox blocks and assembles the
packet. It hands the packet to the ordinary `FujiBusTransport`, which routes
it to a device. The device's response is stored until the Mac reads it.

**Limits.**
* An image of 65,520 blocks or more gets no mailbox, since 16 more blocks
  would exceed the Mac's limit. It still works as a disk.
* The volume itself is an ordinary HFS disk.

marciot's `mac68k-fujinet-serial` pioneered data over DCD block I/O, using a
knock sequence and a magic file. The mailbox past the end of the volume needs
neither.

---

## 5. fujinet-nio changes (patch `0006-fujinet-nio-mac-floppy-bus`)

### 5.1 `MacFloppyFramer`
Files: `include/fujinet/io/transport/mac_floppy_framer.h`,
`src/lib/transport/mac_floppy_framer.cpp`.

It is an `IFramer`: it sits between a `Channel` (bytes) and the stock
`FujiBusTransport`, so FujiBus parsing, encoding and routing are reused
unchanged.

* **`poll()`** reads the channel into a buffer and runs complete commands
  (`R`, `W`, `T`, `A`..`D`, floppy commands, `?`, `!`, `#`, `P`). It then
  announces unit and floppy changes, unless in polled mode.
* **HD20 blocks** map to `DiskService` sectors, so 256- and 512-byte-sector
  images both work. The status block matches the classic firmware's: device
  flags, block count including the mailbox, the FujiNet icon with the unit
  number, and "FujiNet_D".
* **Mailbox** reads and writes are handled as in section 4. `nextPacket()`
  hands requests to the transport, and `sendPacket()` stores responses.
* **Floppy.** `encode_track()` reads a track's sectors and runs the GCR
  encoder. `write_track()` decodes a returned track twice around, so a field
  that crosses the index is found. It writes only good sectors that differ
  from the image. Eject goes through `DiskDevice::eject()`, which unmounts
  the slot and forgets its saved mount.
* **Disks are resolved lazily.** Platforms register the disk device after
  (ESP32) or before (POSIX) the transports. The framer finds it on first use
  and serves no disks until then.
* **Pending (config) mounts are retried** every 10 seconds until they open. A
  TNFS boot image can only open once WiFi is up. A failed open blocks for
  the TNFS timeout, so the interval leaves the main loop room to start WiFi.
* **Resync.** A command left incomplete for 200 ms is dropped, so a lost byte
  cannot wedge the link.
* **Floppy on or off.** The floppy is only offered on the emulator (TCP)
  link, because the ESP32 cannot stream tracks to the board yet.

### 5.2 GCR codec
`include/fujinet/disk/mac_gcr.h`, `src/lib/disk/mac_gcr.cpp`: the FujiNet Mac
firmware's `macGCR` encoder and decoder, ported unchanged.

### 5.3 Profiles, builds, boards
* **`profile.h`:** `Machine::Mac68k` and `TransportKind::MacFloppy`.
  `bootstrap.cpp` builds the framer and transport.
* **POSIX:** build profile `mac_floppy_tcp.cpp` (TCP server channel), CMake
  option `FN_BUILD_MAC_FLOPPY_TCP`, preset `mac-floppy-tcp-debug`.
* **ESP32:** profile `FN_BUILD_ESP32_MAC_FLOPPY` in
  `src/platform/esp32/build_profile.cpp`.
  * Pinmap `FN_PINMAP_MAC_REV0`, taken from the firmware's `mac_rev0.h`: the
    Pico link is UART2, RX GPIO33 and TX GPIO26; SD is 23/19/18/5; LEDs are
    2 and 12.
  * Board `boards/fujimac-rev0-8mb.json` and platform
    `pio-build/ini/platforms/platformio-mac-floppy-fujimac-rev0.ini`, built
    with `FN_DEBUG` so app logs appear on UART0.
  * sdkconfig map entry, including the new `uart-iram` fragment.
  * `channel_factory.cpp` forces the Pico link to 2 Mbaud.
* **Images:** `.hda`, `.hfv` and `.dsk` mount as 512-byte-block raw images
  (`image_probe.cpp`).

### 5.4 Fixes to shared NIO code, found on real hardware
* **`boot_mount.cpp`.** A network boot image (`tnfs://`, `http(s)://`) is
  staged as a pending mount without checking that it exists. The check ran
  about a second after power-on, before WiFi, and dropped the boot disk.
* **`uart_channel.cpp`** (ESP32). The RX FIFO interrupt threshold is lowered
  from 120 to 32 bytes. At 2 Mbaud the driver's default left 8 bytes (40 µs)
  of slack, so WiFi activity overflowed the FIFO in the middle of a 516-byte
  block write.
* **`uart-iram.defaults`**, `CONFIG_UART_ISR_IN_IRAM=y`: the UART interrupt
  keeps running while flash operations disable the cache.
* **`main_esp32.cpp`:** the `macbus` log tag at info level.

### 5.5 Tests and docs
* `tests/test_mac_floppy_framer.cpp` covers:
  * DCD status and blocks;
  * a two-block mailbox exchange;
  * floppy tracks: encode, round trip, a rewritten sector decoded into the
    image, step, motor and eject;
  * polled mode.

  The full suite passes: **387/387**.
* `docs/mac_floppy_bus.md` is the protocol reference inside NIO.

---

## 6. fujinet-nio-lib: the `mac68k` target (patch 0007)

* **Transport:** `src/platform/mac68k/fn_transport.c`, the mailbox client
  from section 4.
  * The 8 KB block buffer is word-aligned. A 68000 raises an address error
    on a word access to an odd address.
  * The drive-queue header is read from low memory, because Retro68 ships no
    `GetDrvQHdr` glue.
* **Build:** `makefiles/compiler-macgcc.mk` (Retro68 `m68k-apple-macos-gcc`,
  `-mcpu=68000`, function and data sections), the target tables, and
  `make mac68k`.
* **Result:** Mac programs get the same `fn_*` API as the Amiga and other
  platforms: clock, network (HTTP/TCP), disk mount and info, raw calls.

---

## 7. The Mac apps (`mac/apps`, Retro68, plain Toolbox windows)

* **FujiNetProbe** (56 KB) calls `fn_init`, then:
  * reads the FujiNet clock (`fn_clock_get`);
  * does an HTTP GET of live weather (`fn_open`/`fn_read`), converting UTF-8
    to MacRoman;
  * mounts `Floppy800.dsk` in slot 5 (`fn_disk_mount`), in the emulator.
* **FujiNet Disks** (55 KB) is a small CONFIG:
  * It lists images through the FileService (`fn_raw_call`, ListDirectory).
    It tries `host:/mac/`, then `flash:/mac/`, then `sd0:/`.
  * It shows slots 1-5 (`fn_disk_info`). Keys 1-5 mount the selected image
    and Command-1..5 eject it; the floppy is ejected through the Mac's own
    `Eject`.
  * The Plus keyboard has no arrow keys, so it also takes j/k and
    double-clicks.

Retro68's `CONSOLE` apps pull in libstdc++ iostreams (about 900 KB) and did
not run, so both apps draw into ordinary windows.

---

## 8. Snow (branch `fujinet-dcd`, `mac/snow/*.patch`)

* **`core/src/mac/swim/dcd.rs`:** a DCD chain on the external port (!ENBL2).
  * The phase-state machine, !HSHK on the sense line, and 7-to-8 receive and
    send with holdoff and resume, bit-timed at 16 cycles per bit through the
    IWM data and write registers.
  * The CA3 daisy chain.
  * Commands go over the TCP link, `SNOW_FUJINET_DCD=host:port`.
* **Floppy:** NIO's floppy goes in the external drive, built from `#` tracks
  (`FloppyImage::from_bitstreams`). Changed tracks go back with `P` when the
  motor stops, and a Mac eject sends `7`.
* **Polled link:** `!` at connect, then `?` every 100 ms from housekeeping.
* **`testrunner/fnrun`:** a scripted headless runner (mouse, keys, waits,
  screenshots). Every emulator result here came from it.

---

## 9. Real hardware: what ran on 2026-09-28

* **ESP32 build:**
  `yes y | ./build.sh -s mac-floppy-fujimac-rev0` and
  `pio run -e mac-floppy-fujimac-rev0 -t upload --upload-port /dev/cu.usbserial-1430`
  flash the firmware.
  `PLATFORMIO_DATA_DIR=mac/run/esp32-data pio run ... -t uploadfs` flashes
  LittleFS with `fujinet.yaml`. That file holds the WiFi settings and the
  boot mount:
  `tnfs://fujinet.diller.org/APPLE/68k/RW/FujiNet51.hda`, read/write.
* **Backup:** the classic firmware was backed up first (the full 8 MB flash)
  to `mac/run/backup/esp32-classic-firmware-8mb.bin`. `esptool.py write_flash
  0 <file>` restores it.
* **Pico:** unchanged.
* **The disk:** `FujiNet51.hda`, an 8 MB HFS volume built on the host with
  hfsutils.
  * It has the boot blocks and blessed System Folder of System 5.1, which
    runs on a 512Ke (6.0.8 does not), plus FujiNetProbe and FujiNet Disks.
  * It was uploaded with `mac/tools/tnfs_put.py`, which seeks before every
    write so retries are safe (about 20 KB/s).
* **Startup timeline (ESP32 log):**

  | Time | Event |
  | --- | --- |
  | 1.1 s | boot mount staged |
  | 5.6 s | first TNFS try fails, no network yet |
  | 10.2 s | WiFi IP |
  | 11.4 s | TNFS session |
  | 11.6 s | unit 0 opened, `h 1` to the Pico |
* **Boot:** on power-up the Mac probed the chain and booted System 5.1 from
  the TNFS image to the Finder. That took 220 block reads and 22 writes,
  with no UART overflows, timeouts or protocol mismatches.

### 9.1 Problems found on the real board, in order
1. **"Number of DCD's mounted: 0."** On the ESP32 the disk device is
   registered after the transports, so the framer took its no-disks
   fallback. Fixed by resolving the disks lazily.
2. **Flashing "?".** The first volume was not bootable (zero boot blocks).
   Fixed by adding boot blocks, the System Folder and blessing.
3. **Writes timed out with the image in LittleFS.** LittleFS is
   copy-on-write, so rewriting a block in the middle of a 2 MB file rewrites
   the rest of the file. That takes seconds, longer than the Pico's 2 s
   timeout. Serving images from TNFS (or an SD card) avoids it.
4. **Read-only is not an escape.** The Finder refuses a locked volume
   without a Desktop file, and the ROM honours write protection. Tested in
   Snow.
5. **The TNFS boot mount was dropped at power-on.** Fixed in
   `boot_mount.cpp`, plus the 10 s pending-mount retry. A 2 s retry that
   blocked for 4.5 s starved the loop, and WiFi never started.
6. **WiFi credentials** were first taken from the firmware's empty `SSID=`
   template; they now come from the saved config.
7. **"System on this disk may be damaged."** System 6.0.8 does not run on a
   512Ke. Fixed by building the disk with System 5.1.
8. **Watch cursor, then a reboot.** The ESP32 UART FIFO overflowed during
   block writes (`UART FIFO overflow` in the log). Fixed with the RX
   threshold of 32, the ISR in IRAM, and the framer resync.
9. **Flashing "?" after the failures.** The USB-powered Pico kept a confused
   DCD state across Mac power cycles. Fixed by `picotool reboot -f`, then an
   ESP32 reset so NIO announces its units again. NIO announces on change
   only, so a Pico restarted on its own does not hear the unit count; see
   section 11.

---

## 10. Emulation (verified in Snow, Mac Plus, System 6.0.8)

* HD20 served by NIO; boots with no floppy.
* A Finder duplicate of 400 KB is byte-identical.
* FujiNetProbe: clock, HTTP GET, floppy mount.
* NIO floppies in the external drive. Writes are decoded back into the
  `.dsk` (resource data identical), and an eject unmounts and forgets the
  slot.
* FujiNet Disks browses, mounts and ejects.
* `mac/tools/e2e.sh` runs it all headless. `mac/tools/pico_sim.py` checks the
  protocol without an emulator.

---

## 11. Known limits and next steps

* **No floppy on the real board yet.** The ESP32 side needs RMT track
  streaming and the Pico's `w` write-capture frames.
* **Re-announce on Pico reset.** NIO should re-announce `h` n when the Pico
  restarts, for example on a `?`, or periodically. The classic firmware has
  the same gap.
* **HD20 slots take effect at restart,** because the ROM probes only at
  startup.
* **No mailbox on full-size images** (65,520 blocks or more).
* **LittleFS images suit reading only;** use TNFS or an SD card for
  read/write disks.
* **The FujiBus apps on real hardware** (FujiNetProbe, FujiNet Disks from the
  TNFS disk) are the next thing to run on the booted Mac.
* **A fuller Mac CONFIG:** TNFS hosts, the slot catalogue, names in the slot
  list.

---

## 12. Where everything is

| Path | What |
| --- | --- |
| `patches/0006-fujinet-nio-mac-floppy-bus.patch` | All NIO changes (sections 5.1-5.5), applied by `scripts/02-clone-workspace.sh` |
| `patches/0007-fujinet-nio-lib-mac68k.patch` | fujinet-nio-lib `mac68k` target |
| `mac/snow/*.patch` | Snow changes (also branch `fujinet-dcd` in `~/code/snow`) |
| `mac/apps/fnprobe`, `mac/apps/fndisks` | Mac apps |
| `mac/tools/` | `run-nio.sh`, `run-snow.sh`, `e2e.sh`, `pico_sim.py`, `make-fujinet-volume.sh`, `tnfs_put.py` |
| `mac/README.md` | How to build and run |
| `mac/evidence/` | Screenshots from the verified runs |
| `mac/run/` (not committed) | ROM, System disks, NIO state, ESP32 data image (holds the WiFi passphrase), firmware backup |
