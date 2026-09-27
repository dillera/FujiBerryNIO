# fujinet-nio.device: DOSBase links to libnix's auto-open `-1`; KS 1.3 / 68000 address error on open

**Repo:** markjfisher/fujinet-nio-driver (seen at `3e80634`, workspace `ac1d791`)
**Affects:** `fujinet-nio.device` (serial broker) and `fujinet-nio-native-test.device`, all Kickstarts; crashes outright on a 68000
**Patch:** `patches/0004-driver-nio-dosbase-null.patch` (two lines)

## Symptom

On Workbench 1.3.3 / Kickstart 34.5 / A500 (68000), via the workspace's
`wb13` env (`test_wb13_cold_stock_serial_worker`), or by hand:

```text
C:fujinet-load-resident DEVS:fujinet-nio.device fujinet-nio.device
C:fujinet-nio-exchange --type clock --backend cold --baud 38400 --trials 1
```

the guest shows **"Software error - task held"**, and Amiberry logs a 68000
address error in the loaded device, at the same PC on every run:

```text
Exception 3 (00000c6e c5250a) at c5250a -> fc081a!
```

## Diagnosis

Disassembly of the live guest at the fault (Amiberry IPC `DISASSEMBLE`):

```text
00c524ea 2c79 00c1 9b14   MOVEA.L $00c19b14 [ffffffff],a6    ; A6 = DOSBase
...
00c52506 0c6e 0023 0014   CMP.W #$0023,($0014,a6)            ; DOSBase->dl_lib.lib_Version
00c52526 4eae ff76        JSR (-$008a,a6)                    ; CreateProc()
```

The byte pattern `0c6e 0023 0014` occurs only in `fujinet-nio.device`,
inside `open_backend()`'s serial-worker start:

```c
if (DOSBase == NULL)
    DOSBase = (struct DosLibrary *)OpenLibrary("dos.library", 34);
...
if (DOSBase->dl_lib.lib_Version >= 36) {
```

`DOSBase` held **`0xFFFFFFFF`**, so the NULL test was skipped and
`lib_Version` was read at `0xFFFFFFFF + 0x14`, an odd address. That is an
address error on a 68000.

## Root cause

`nio.device/fujinet_nio_device.c` declares

```c
struct DosLibrary *DOSBase;
```

This is a tentative (common) definition. bebbo's toolchain libc also defines
`DOSBase` in `lib_a-__dosbase.o`, as a libnix auto-open entry in
`__LIB_LIST__`, initialised to `-1` with the name `"dos.library"`:

```text
Disassembly of section .dlist___LIB_LIST__:
00000000 <_DOSBase>:  ffff ffff  0000 0000   ; -1, RELOC32 -> "dos.library"
```

Normally program startup code opens every library in that list and replaces
the `-1`. The device links with `-nostartfiles`, so nothing ever replaces it.
The link map shows the merge:

```text
libc.a(lib_a-__dosbase.o)  ../build/amiga/nio.device/fujinet_nio_device.o (DOSBase)
0x00002b88  DOSBase                       ; in .data, not .bss
```

`nio.device/fujinet_nio_directory_backend.c` has the same declaration, and
therefore the same problem, in `fujinet-nio-native-test.device`.

## Fix

Make both of them real definitions, so the device gets its own zeroed `.bss`
slot and `__dosbase.o` is no longer pulled into the link:

```diff
-struct DosLibrary *DOSBase;
+struct DosLibrary *DOSBase = NULL;
```

After the fix, `m68k-amigaos-nm` shows `B _DOSBase` (it was `D`), and
`__dosbase` no longer appears in either device's map.

## Verification

- `make native resident-artifacts tests`: builds clean, ROMTag gate OK, and all
  native contract suites pass.
- WB1.3 / a500-000: `test_wb13_cold_stock_serial_worker` now **passes**. It
  passed twice, each time in a fresh Amiberry process. The guest shows:
  ```text
  Resident loaded: fujinet-nio.device
  installed_backend=serial lifecycle=cold
  req_len=6 resp_len=19 ... result=0 cause=0 native=0 status=0 backend=cold
  ```
  and the host logs the clock exchange on device `0x45`.
  `test_wb13_ofs_mount_without_globvec` also passes.
- WB3.1: `test_isolated_exchange` previously produced **no FujiBus traffic**,
  because it never got past `open_backend`. With the fix it passes ISOLATED,
  EXCHANGE, REUSE, RESIDENT and AFTER_OPENCNT0. It then stalls in the TIMEOUT
  phase, which is a separate issue that this bug had been hiding.
- WB3.1 `test_native_test_clock_exchange` and
  `test_exchange_tool_installation_parity[serial,native]` still pass.

## Suggested hardening

Any other `-nostartfiles` binary that declares an auto-opened library base
tentatively (`DOSBase`, `IntuitionBase`, `GfxBase`, …) is exposed to the same
merge. Either define each base explicitly as `= NULL`, or add
`-fno-common` to the device `CFLAGS`, so that each object has to own its
definition.
