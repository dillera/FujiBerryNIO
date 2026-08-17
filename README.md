# Amiberry + FujiNet NIO on macOS

Reproducible setup for running the [fujinet-nio-workspace][ws] Amiga test stack
on an Apple Silicon Mac, following [`docs/amiga/amiberry-testing.md`][doc]
and adapting it where that document assumes Linux and licensed AmigaOS 3.2.

Verified on **macOS 26.5.2 (arm64)**, Amiberry **8.3.0**, Homebrew 6.0.

[ws]: https://github.com/markjfisher/fujinet-nio-workspace
[doc]: https://github.com/markjfisher/fujinet-nio-workspace/blob/master/docs/amiga/amiberry-testing.md

---

## What this stack is

```
Amiga guest (AmigaOS, Amiberry)
   |  serial.device
   v
Amiberry TCP serial endpoint  127.0.0.1:23462   (serial_direct=true)
   |
   v  socat bridge
FujiNet NIO (POSIX build)     127.0.0.1:65504   fujibus-tcp-debug
```

The Amiga apps (`wifitest`, `fhost`, `fls`, `fmount`, …) are cross-compiled for
m68k, packed into a bootable HDF next to an expanded AmigaOS tree, and booted in
Amiberry. Their FujiBus traffic goes out the emulated serial port and into the
host-side FujiNet NIO service.

`serial_direct=true` matters: without it Amiberry's emulated serial reader drops
bytes from binary FujiBus frames.

---

## Quick start

```bash
scripts/01-install-toolchain.sh      # m68k-amigaos-gcc  (long: ~40-60 min)
scripts/02-build-amigaos-tree.sh     # expand AmigaOS 3.1 ADFs -> assets/
scripts/03-configure-workspace.sh    # write workspace local/config.env + wb3.1
scripts/04-run.sh                    # build apps + HDF, boot in Amiberry
```

Once booted, open `System/Shell` in Workbench and run:

```text
wifitest
fhost
fls
```

---

## Layout

| Path | What it is |
| --- | --- |
| `scripts/` | The four setup steps, in order |
| `workspace/` | Clone of fujinet-nio-workspace (submodules included) |
| `toolchain-src/` | Clone of bebbo's amiga-gcc (build tree) |
| `assets/amigaOS3.1/` | Expanded AmigaOS tree — **licensed data, not committed** |
| `config/config.env` | Template installed as `workspace/local/config.env` |
| `patches/` | Portability patches applied to upstream sources |
| `docs/` | Adaptation notes |
| `logs/` | Build logs |

---

## Prerequisites

```bash
brew install --cask amiberry
brew install socat bash wget make lhasa gmp mpfr libmpc flex gettext \
             gnu-sed texinfo autoconf bison uv cmake ninja
```

Amiberry must be **8.3.0 or newer** — earlier releases have the breakpoint and
stepping bugs the debugger IPC section of the upstream doc calls out. The
Homebrew cask build does include IPC socket support, including the debugger
commands:

```bash
strings /Applications/Amiberry.app/Contents/MacOS/Amiberry | grep DEBUG_ACTIVATE
```

### Licensed assets you must supply

Nothing here downloads AmigaOS. You need, from your own licensed media:

* A Kickstart 3.1 ROM (this setup uses **40.63 / A600**, the ECS ROM matching
  the emulated 68000). 40.68 / A1200 is AGA-oriented.
* The **Workbench 3.1 rev 40.42** six-disk ADF set.

Point `scripts/02-build-amigaos-tree.sh` at them with `TOSEC_ROOT`, `WB_SET`
and `KICKSTART_SRC` if your paths differ from the defaults.

---

## Adaptations from the upstream document

The upstream doc targets Linux and licensed AmigaOS 3.2. Everything below is a
deviation, with the reason.

### 1. AmigaOS 3.1 instead of 3.2

Upstream's default `wb3.2` profile wants
`ROM/kickCDTVa1000a500a2000a600.rom`, `ADF/Workbench3.2.adf` and
`L/FastFileSystem` from a paid Hyperion AmigaOS 3.2 release.

`scripts/02-build-amigaos-tree.sh` builds the same asset-root layout from an
AmigaOS 3.1 disk set, mirroring what the 3.1 HD installer does: Workbench and
Extras to the volume root, then `Storage/`, `Locale/`, `Fonts/`, plus
`L/FastFileSystem` from the Install disk.

3.1 turned out to carry everything the HDF builder reads: the Workbench disk
has `Devs/serial.device`, and its `S/Startup-Sequence` has the `LoadWB` marker
the builder splits on.

A matching `wb3.1` profile is added to `configs/amiga/workbenches.yaml`.
`wb3.2` is left untouched, so this repo still works for anyone who does own 3.2.

**Known limitation:** `--with-driver` is not supported on 3.1. It prepends
`C:LoadModule DEVS:fujinet-disk.device` to the startup sequence, and
`LoadModule` is an AmigaOS 3.2 command. `scripts/04-run.sh` refuses the flag
rather than producing an image that fails at boot.

### 2. Toolchain at `$HOME/opt/amiga`

The workspace defaults to `/opt/amiga`, which needs root on macOS. We install to
`$HOME/opt/amiga` and set `AMIGA_TOOLCHAIN_BIN` — a documented workspace
override — in `local/config.env`.

### 3. Amiberry is an `.app`, not a PATH binary

The cask installs `/Applications/Amiberry.app`, so there is no `amiberry` on
`PATH`. `AMIBERRY_BIN` points at the bundle executable.

### 4. SDL video driver

The upstream doc mentions the runner defaulting `SDL_VIDEO_DRIVER` to
`kmsdrm,wayland,x11`. The checked-out revision of `tools/amiga_emulator/run.py`
sets no such default, and on macOS SDL selects `cocoa` on its own. No change
needed.

---

## Upstream bugs fixed along the way

These are real portability defects, not local preferences. Patches are in
`patches/`; they are worth sending upstream.

### fujinet-nio (`patches/0001-fujinet-nio-macos-portability.patch`)

| File | Problem |
| --- | --- |
| `include/fujinet/io/devices/clock_commands.h` | uses `std::size_t` without `<cstddef>`; libstdc++ pulls it in transitively, libc++ does not |
| `src/lib/tcp_channel.cpp` | calls `select()` without `<sys/select.h>` |
| `src/platform/posix/wifi_link.cpp` | `#if defined(__linux__)` guards hide `<sys/socket.h>` etc., so `sockaddr` is undeclared on macOS even though the signature uses it |

The third is guarded as `__linux__ || __APPLE__` rather than stubbed: Darwin has
`getifaddrs`, `freeifaddrs` and `inet_ntop` with identical semantics, so the
real implementation compiles and works.

After the patch the host build is clean and its own suite passes:
`265 test cases | 5713 assertions | 0 failed`.

### amiga-gcc (handled inside `scripts/01-install-toolchain.sh`)

| Symptom | Cause and fix |
| --- | --- |
| `configure: error: Building GDB requires GMP 4.2+, and MPFR 3.1.0+` | The Makefile's Darwin branch passes only `--with-libgmp-prefix`; the bundled GDB configure wants `--with-gmp`/`--with-mpfr`, and Homebrew's `/opt/homebrew` is not on the default search path. We restate `CONFIG_BINUTILS` with both spellings. |
| `_stdio.h:322:7: error: expected identifier or '('` | GCC 6.5's bundled zlib reads `TARGET_OS_MAC` as *classic* Mac OS and does `#define fdopen(fd,mode) NULL`, which mangles the real `fdopen` declaration. Fixed with `--with-system-zlib`. |
| `fibonacci_heap.h:481: error: reference to non-static member function must be called` | GCC 6.5 typo: `heapb->min->compare (heapa->min)` should be `m_min` — the next line already says `heapa->m_min = heapb->m_min`. GCC defers the lookup in an uninstantiated template; clang does not. Patched in place. |
| `libtool: No such file or directory` (Error 127), `No rule to make target 'math/.deps/acoshq.Plo'` | Parallel-make race in GCC's target libraries: compilation starts before configure writes `libtool`. The target-library phase is built with `-j1`. |
| `SDL_systimer.c:85: error: conflicting types for 'TimerBase'` | `libSDL12` does not build and nothing here uses it. Dropped from the target list. |

Note also that upstream's `all` target does **not** include `clib2`, which
`nio-apps` requires (`-mcrt=clib2`). We add it explicitly.

### fujinet-nio-workspace (`patches/0002-*`)

`scripts/build-amiga-test-disk` needed four changes:

| Symptom | Cause and fix |
| --- | --- |
| `FSError: File not found: WBStartup/Welcome` | The first-run Welcome program is an AmigaOS 3.2 addition. Deletion made tolerant via a `run_xdf_optional` helper. |
| `FSError: Name already exists: serial.device` | The builder imports `Devs/serial.device` from the boot ADF, but an expanded 3.1 tree already ships it and xdftool will not overwrite a node. Delete before write. |
| Volume boots labelled `AmigaOS3.2` | `xdftool pack` takes the volume name from the staging directory, which was hardcoded. Now derived from the OS tree's own name. |
| **`C:C:Assign T: RAM:` in the generated Startup-Sequence** | The builder split the licensed Startup-Sequence at the *word* `LoadWB`. AmigaOS 3.1 writes `C:LoadWB`, so the leftover `C:` was glued onto the first injected command and that line failed at boot. Now splits at the start of the line. |

That last one is **not macOS-specific** — it is a latent 3.1 incompatibility that
would occur on Linux too.

### fujinet-nio-driver (`patches/0003-*`)

| Symptom | Cause and fix |
| --- | --- |
| `error: unknown type name 'BPTR'` | `disk.device/fujinet_disk_device.c` uses `BPTR` for its seglist without including `<dos/dos.h>`; NDK 3.2 does not pull it in transitively. Added the include, plus a matching stub header so the native (host) resident test still builds. |
| ~24 × `format '%lu' expects 'long unsigned int', but argument has type 'ULONG {aka unsigned int}'` | NDK 3.2's `<exec/types.h>` switches `ULONG` from `unsigned long` to `uint32_t` whenever `__STDC_VERSION__ >= 199901`, and on this toolchain `uint32_t` is `unsigned int`. Both are 32-bit, so the arguments are cast to `unsigned long`/`long` at the call sites — correct under either NDK. |
| `error: variable 'active' is uninitialized when passed as a const pointer argument` | clang 21's `-Wuninitialized-const-pointer` vs `-Werror`. The variables are address-as-token placeholders whose values are never read; initialising them changes nothing. |

### nio-apps / nio-core-apps (`patches/0004-*`, `0005-*`)

| Symptom | Cause and fix |
| --- | --- |
| `undefined reference to '_impure_ptr'` (doslistdiag), `'__locale_ctype_ptr'` (fboot) | `CFLAGS` omitted `-mcrt=clib2` while `LDFLAGS` had it. `-mcrt` selects the runtime's **headers** as well as its libraries, so objects compiled against newlib were linked against clib2. Added `-mcrt=clib2` to `CFLAGS`. **Also not macOS-specific.** |
| `error: size of array 'uint32_is_ulong' is negative` | `sizetest` asserts `__builtin_types_compatible_p(uint32_t, unsigned long)`. On this toolchain `uint32_t` is `unsigned int` for both newlib and clib2. The three `_Static_assert`s that matter (everything is 32 bits) still pass, so the type-name check now reports at runtime instead of failing the build. |

---

## Verification

Both end-to-end paths from the upstream doc were run and pass.

`wifitest` (`AMIGA_TEST_PROJECT=apps`), extracted from the guest HDF:

```text
FujiNet-NIO Wi-Fi API test
link=0 enabled=0 RSSI=0
IP= gateway= DNS=
BSSID=(none)
SSID= BSSID= password=not set
scan count=1 more=1
  FujiNet-Sim channel=1 RSSI=-42 BSSID=02:00:00:00:00:01
Wi-Fi API test passed
```

with matching FujiBus frames on the host side (device `0xF3`, the Wi-Fi device):

```text
fujibus: receive: id=3 dev=0xF3 cmd=0x04 params=0 payload=4
fujibus: send: dev=0xF3 status=0 cmd=0x04 payload=24
  0000: 01 01 01 0b 46 75 6a 69 4e 65 74 2d 53 69 6d 02  |....FujiNet-Sim.|
```

`fhost` (`AMIGA_TEST_PROJECT=core`) returns `HOST: (none) / PATH: (none)` for an
unconfigured host, talking to device `0xF0` (HostService).

Reproduce either with:

```bash
cd workspace && ./scripts/build.sh amiga-e2e -- --timeout 90
```

```bash
cd workspace && AMIGA_TEST_PROJECT=core AMIGA_TEST_APP=fhost \
  ./scripts/build.sh amiga-e2e -- --timeout 90
```

Read a result out of the image with:

```bash
uvx --from amitools xdftool workspace/build/images/amiga-wifitest.hdf type wifitest.result
```

**Gotcha:** run those in a shell that has *not* already sourced
`scripts/env.sh`. `env.sh` sets `AMIGA_TEST_COMMAND` from `AMIGA_TEST_APP` with
`:-`, so a previously exported value sticks and you will silently run the old
app. Use `env -u AMIGA_TEST_COMMAND -u AMIGA_TEST_APP ...` if in doubt.

---

## Troubleshooting

**`xdftool unpack` nests output under a volume-name directory.** It extracts
directly into the destination only when that directory does not already exist;
otherwise it creates `<VolumeName>/` plus `.blkdev`/`.bootcode`/`.xdfmeta`
sidecars. `scripts/02-build-amigaos-tree.sh` removes the destination first.

**Rebuilding just the toolchain step that failed.** The amiga-gcc build is
incremental. Delete the relevant stamp under
`toolchain-src/build-Darwin-m68k-amigaos/` and re-run
`scripts/01-install-toolchain.sh`.

**Checking the Amiberry IPC socket.** The runner prints the path and writes it
to `workspace/build/amiga-e2e/amiberry.sock.path`:

```bash
workspace/scripts/amiberry-ipc GET_STATUS
workspace/scripts/amiberry-ipc SCREENSHOT /tmp/screen.png
```
