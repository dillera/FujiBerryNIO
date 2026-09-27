# Amiberry + FujiNet NIO on macOS

Reproducible setup for running the [fujinet-nio-workspace][ws] Amiga test stack
on an Apple Silicon Mac, on two systems:

* **Workbench 1.3** — Kickstart 1.3 (34.5), A500, 68000, 512K chip + 512K slow
* **Workbench 3.1** — Kickstart 3.1 (40.63), built by upstream's six-disk builder

Verified on **macOS 26.5.2 (arm64)**, Amiberry **8.3.0**, workspace
`ac1d791` (2026-09-27).

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

Amiga drivers and apps are cross-compiled per Workbench **artifact profile**
(`wb13` links the Kickstart 1.3 `nix13` runtime, `wb31` links `clib2`), packed
into a test HDF or mounted read-only as `NIO:`, and talk FujiBus over the
emulated serial port to the host-side FujiNet NIO service.

---

## Quick start

```bash
scripts/01-install-toolchain.sh   # m68k-amigaos-gcc under ~/opt/amiga  (~40-60 min, once)
scripts/02-clone-workspace.sh     # clone/update workspace + submodules, apply patches/
scripts/03-configure-workspace.sh # local/*.env, build the WB3.1 env HDF
scripts/04-build-wb13-hdf.sh      # build the bootable WB1.3 HDF + its E2E env
scripts/05-run.sh                 # boot interactive Workbench 1.3 with NIO:
PROFILE=wb31-a1200 scripts/05-run.sh   # ...or Workbench 3.1
```

Build the host service and Amiga stack, then run the WB1.3 E2E cases:

```bash
cd workspace
./scripts/build.sh fujinet-tcp-debug amiga
scripts/amiga-tests --amiga-env wb13 --amiga-machine a500-000 -k wb13 -v
```

In an interactive WB1.3 session, install the drivers onto the HDF with
`Execute NIO:Install-FujiNet-WB13`; see upstream's [testing doc][doc].

---

## Layout

| Path | What it is |
| --- | --- |
| `scripts/` | The five steps, in order |
| `config/config.env` | Toolchain and Amiberry paths → `workspace/local/config.env` |
| `config/amiga.env` | Licensed-media path template → `workspace/local/amiga.env` |
| `patches/` | Fixes applied to upstream; see [`patches/README.md`](patches/README.md) |
| `docs/` | Upstream bug report(s) drafted from this work |
| `workspace/` | Clone of fujinet-nio-workspace (not committed) |
| `toolchain-src/` | Clone of bebbo's amiga-gcc build tree (not committed) |
| `assets/amigaOS1.3/` | Generated WB1.3 HDF — **licensed data, not committed** |
| `logs/` | Build and test logs |

---

## Prerequisites

```bash
brew install --cask amiberry
brew install socat bash wget make lhasa gmp mpfr libmpc flex gettext \
             gnu-sed texinfo autoconf bison uv cmake ninja
```

Amiberry must be **8.3.0 or newer** (debugger IPC fixes).

### Licensed assets you must supply

Nothing here downloads AmigaOS. `03-configure-workspace.sh` reads them from
`TOSEC_ROOT` (TOSEC Workbench folder) and `KICK_DIR` (ROM dumps):

| System | Kickstart | Disks |
| --- | --- | --- |
| WB 1.3 | `kick34005.A500` — 1.3 r34.5, plain 256K dump (no `rom.key`) | Workbench 1.3.3 rev 34.34, disk 1 |
| WB 3.1 | `kick40063.A600` — 3.1 r40.63, ECS/68000 | Workbench 3.1 rev 40.42, six-disk set |

---

## The Workbench 1.3 system

Upstream's `wb13` environment copies a "clean, manually installed, bootable
WB1.3 HDF" (`AMIGA_WB13_HDF`) but does not create one.
`scripts/04-build-wb13-hdf.sh` builds it non-interactively, the way a 1.3 hard
disk was set up by hand:

1. Unpack the WB1.3 Workbench ADF **with xdftool's metadata sidecars**, so
   protection bits (`s` on scripts, `p` on pure commands) and the boot block
   survive. 1.3 only runs `S:` scripts that carry `s`.
2. Drop the floppy-only `Addbuffers df0:` line from `S/Startup-Sequence`.
3. Pack a 20 MB FFS hardfile.

Kickstart 1.3 has no FastFileSystem in ROM, so Amiberry loads the WB1.3
`L/FastFileSystem` from the host (`RDB: faked RDB filesystem 444F5301 (DOS\1)
loaded` in its log). The runner finds it beside the env base HDF, which the
`prebuilt_hdf` builder does not copy, so the script puts it there. For the
interactive `wb13-a500` profile, `05-run.sh` points `AMIBERRY_FAST_FILE_SYSTEM`
at the seeded copy.

Persistent interactive images (`workspace/images/amigaos{1.3,3.1}-run.hdf`) are
seeded once and never overwritten, so installs you make in them survive.

---

## Adaptations from upstream

* **Toolchain at `$HOME/opt/amiga`** — upstream defaults to `/opt/amiga`,
  which needs root on macOS; `AMIGA_TOOLCHAIN_BIN` is set in `local/config.env`.
* **Amiberry is an `.app`** — `AMIBERRY_BIN` points into the bundle.
* **Workbench 3.1 rather than 3.2** — 3.2 is a paid Hyperion product. Upstream
  now ships a `wb31` six-disk environment, so this repo no longer builds its own
  3.1 tree or profile.

### amiga-gcc (handled inside `scripts/01-install-toolchain.sh`)

| Symptom | Cause and fix |
| --- | --- |
| `configure: error: Building GDB requires GMP 4.2+, and MPFR 3.1.0+` | The Makefile's Darwin branch passes only `--with-libgmp-prefix`; the bundled GDB configure wants `--with-gmp`/`--with-mpfr`, and Homebrew's `/opt/homebrew` is not on the default search path. We restate `CONFIG_BINUTILS` with both spellings. |
| `_stdio.h:322:7: error: expected identifier or '('` | GCC 6.5's bundled zlib reads `TARGET_OS_MAC` as *classic* Mac OS and does `#define fdopen(fd,mode) NULL`. Fixed with `--with-system-zlib`. |
| `fibonacci_heap.h:481: error: reference to non-static member function must be called` | GCC 6.5 typo (`min` for `m_min`); clang does not defer the lookup. Patched in place. |
| `libtool: No such file or directory` (Error 127) | Parallel-make race in GCC's target libraries; that phase is built with `-j1`. |
| `SDL_systimer.c:85: error: conflicting types for 'TimerBase'` | `libSDL12` is unused here and dropped from the target list. |

Upstream's `all` target does not include `clib2`, which the `wb31`/`wb32`
profiles need; the script adds it.

---

## Verification (2026-09-27)

Host build: `fujinet-nio` **383 test cases, 0 failed**; all Amiga driver
native contract suites pass; `tools/build` unit tests 23 passed.

| Env | Case | Result |
| --- | --- | --- |
| wb13 / a500-000 | `test_wb13_cold_stock_serial_worker` | **pass**, twice, in separate emulator processes |
| wb13 / a500-000 | `test_wb13_ofs_mount_without_globvec` (`DN0:` OFS mount) | **pass** |
| wb31 | `test_cli_arguments_and_persistent_state` | pass |
| wb31 | `test_wifi_configuration_set_get_status_and_scan` | pass |
| wb31 | `test_native_test_clock_exchange` | pass |
| wb31 | `test_exchange_tool_installation_parity[serial,native]` | pass |
| wb31 | `test_fmount_fumount_standard_adf[serial,native]` | fail — `FMOUNT HD RC=10`; fails identically without this repo's patches |
| wb31 | `test_isolated_exchange` | fail — stalls in its TIMEOUT phase; without patch 0004 it fails earlier, with no FujiBus traffic at all |

The WB1.3 cold-broker run, as the guest shows it:

```text
Resident loaded: fujinet-nio.device
installed_backend=serial lifecycle=cold
req_len=6 resp_len=19 elapsed_us=80002 ttfb_us=- result=0 cause=0 native=0 status=0 backend=cold
```

with the host answering the clock request (device `0x45`) and the
completion-marker file listing (device `0xFE`).

Every E2E run leaves logs, the test HDF and per-second screenshots under
`workspace/test-evidence/amiberry-<timestamp>/<case>/`.

---

## Troubleshooting

**`scripts/amiga-tests` exits 1 with no output.** `source scripts/env.sh`
failed; patch 0002 fixes the cause. Check with
`bash -c 'set -e; source workspace/scripts/env.sh; echo ok'`.

**Every E2E case is `SKIPPED ... prerequisites unavailable: amiberry`.**
Patch 0002 not applied, or `AMIBERRY_BIN` not set in `local/config.env`.

**Guest shows "Software error - task held".** Read the fault from
`amiberry.log` (`Exception 3 (...) at <pc>`), then while a run is live:
`AMIBERRY_IPC_SOCKET=/tmp/amiberry.sock workspace/scripts/amiberry-ipc
DISASSEMBLE 0x<pc - 0x20> 16`. Match the bytes against
`m68k-amigaos-objdump -d` of the candidate binary to find the owner.

**A silently stale app runs.** Use `env -u AMIGA_TEST_COMMAND -u AMIGA_TEST_APP`
in a shell that has already sourced `scripts/env.sh`; it sets these with `:-`.

**Rebuilding just the toolchain step that failed.** Delete the relevant stamp
under `toolchain-src/build-Darwin-m68k-amigaos/` and re-run step 01.
