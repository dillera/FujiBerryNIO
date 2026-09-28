# Patches

Fixes applied to upstream sources to build and run this stack on macOS
(Apple Silicon), including Workbench 1.3 on a 68000 A500. Applied
automatically by `scripts/02-clone-workspace.sh`; each also applies by hand
with `git apply` inside the listed repo.

| Patch | Repo | What it fixes |
| --- | --- | --- |
| `0001` | `fujinet-nio` | `tests/directory_packet_io.cpp` calls Linux-only `getrandom()`; use `getentropy()`, which glibc and macOS both declare in `<sys/random.h>` |
| `0002` | `fujinet-nio-workspace` | Three harness bugs, below |
| `0003` | `fujinet-nio-driver` | Native test uses `mkdtemp()` under strict `_POSIX_C_SOURCE`; macOS hides it without `_DARWIN_C_SOURCE` (the file already sets glibc's `_DEFAULT_SOURCE`) |
| `0004` | `fujinet-nio-driver` | **`fujinet-nio.device` crashes Kickstart 1.3 on a 68000** — see below |
| `0005` | `fujinet-nio` | Every `tcp://` session failed on macOS: `step_connect()` read a stale `errno` after `poll()` timed out (poll leaves errno untouched), so a connect still in progress was marked failed and the first write got an I/O error. `poll_connect_complete()` now clears errno when not yet ready |
| `0006` | `fujinet-nio` | **New: Macintosh floppy-port bus.** `MacFloppyFramer` speaks the FujiNet Mac board's Pico protocol (DCD/HD20 block I/O) and carries FujiBus through mailbox blocks past the end of each HD20 volume. Also serves the 800K floppy, using the FujiNet Mac firmware's GCR codec (`mac_gcr`); a Mac eject clears the slot (`DiskDevice::eject`). Also has polled mode for emulators, unit tests, `docs/mac_floppy_bus.md`, and an ESP32 variant for the real board (`mac-floppy-fujimac-rev0`). Build preset `mac-floppy-tcp-debug`. `.hda`/`.hfv`/`.dsk` mount as 512-byte-block raw images. See `mac/README.md` |
| `0007` | `fujinet-nio-lib` | **New: `mac68k` target** (Retro68). The transport is block I/O to that mailbox through the ROM's HD20 driver, so classic Mac programs use the same `fn_*` API as the Amiga |

## 0004: the Kickstart 1.3 crash (not macOS-specific)

Symptom: on WB1.3 / KS 34.5 / 68000, `fujinet-load-resident
DEVS:fujinet-nio.device` → *Software error - task held*; Amiberry logs
`Exception 3` (address error) inside the device.

`nio.device` declares `struct DosLibrary *DOSBase;` — a tentative
definition. bebbo's toolchain libc also defines `DOSBase` (in
`__dosbase.o`) as a libnix auto-open entry initialised to `-1`, which program
startup code replaces with a real `dos.library` base. The linker merged the
two, so the device got the `-1`. It links with `-nostartfiles`, so nothing ever
replaced it. `open_backend()`'s `if (DOSBase == NULL) OpenLibrary(...)` was
therefore skipped and `DOSBase->dl_lib.lib_Version` was read through
`0xFFFFFFFF + 0x14` — an odd address, which a 68000 faults on.

Verified in the running guest: the instruction before the fault is
`MOVEA.L DOSBase,A6` and the global held `$FFFFFFFF`. With `DOSBase = NULL`
the link no longer pulls in `__dosbase.o`, the value is a real `.bss` zero, and
the device opens `dos.library` itself.

The same `-1` also broke WB3.1: `test_isolated_exchange` reached FujiNet with
zero frames before the fix; after it, the broker's EXCHANGE, REUSE, RESIDENT
and AFTER_OPENCNT0 phases pass (its later TIMEOUT phase still stalls — an
upstream issue that was hidden until now).

## 0002: workspace harness

| File | Problem |
| --- | --- |
| `scripts/env.sh` | `pathadd_end` returned 1 for a missing dir. It is the last command in `setup_nio_environment` (for `/opt/watcom/binl`), so `source scripts/env.sh` failed and every `set -e` caller — e.g. `scripts/amiga-tests` — exited silently with status 1 on any machine without Open Watcom |
| `integration-tests/amiberry/conftest.py` | Prerequisite check required `amiberry` on `PATH`, skipping every E2E case although the runner itself launches `AMIBERRY_BIN` (a path inside the macOS `.app`) |
| `tools/build/nio_build/amiga_config.py` | Profiles pass `rom_key: ${AMIGA_WB13_ROM_KEY}`; with a plain (non-Amiga Forever) ROM that variable is unset, the literal `${...}` became a path, and `wb13-a500` refused to launch. An unset or empty optional key now means "no key", as the docs describe |

## Retired

The previous `0001`–`0005` (fujinet-nio includes, AmigaOS 3.1 test-disk
builder, NDK 3.2/clang driver fixes, `-mcrt=clib2` in the app makefiles) were
merged upstream or superseded by upstream's `scripts/amiga-env`. They are in
this repo's git history.
