# Patches

Portability fixes applied to upstream sources to build and run this stack on
macOS (Apple Silicon) with an AmigaOS 3.1 asset tree and the NDK 3.2 that ships
with bebbo's amiga-gcc. Apply with `git apply` inside the matching repo.

| Patch | Repo | What it fixes |
| --- | --- | --- |
| `0001` | `fujinet-nio` | Missing `<cstddef>` / `<sys/select.h>` includes and Linux-only `#if` guards that hide `sockaddr` on macOS |
| `0002` | `fujinet-nio-workspace` | AmigaOS 3.1 support in `build-amiga-test-disk` (optional `WBStartup/Welcome`, overwrite `Devs/serial.device`, volume name from the OS tree, line-based `LoadWB` split) plus the `wb3.1` profile |
| `0003` | `fujinet-nio-driver` | `<dos/dos.h>` for `BPTR`, a matching stub header for the native tests, `%lu`/`%ld` argument casts for NDK 3.2's `uint32_t`-based `ULONG`, and an initialised token variable for clang's `-Wuninitialized-const-pointer` |
| `0004` | `nio-apps` | `-mcrt=clib2` in `CFLAGS` so headers match the linked runtime; `sizetest` reports rather than asserts `uint32_t == unsigned long` |
| `0005` | `nio-core-apps` | `-mcrt=clib2` in `CFLAGS` (same runtime mismatch) |

## The two that are not macOS-specific

`0002`'s `LoadWB` change and `0004`/`0005`'s `-mcrt=clib2` are latent bugs that
would bite on Linux too:

* `build-amiga-test-disk` split the Startup-Sequence at the *word* `LoadWB`.
  AmigaOS 3.1 writes `C:LoadWB`, so the leftover `C:` was glued onto the first
  injected command, yielding `C:C:Assign T: RAM:`, which fails at boot. The
  patch splits at the start of that line instead.
* `CFLAGS` omitted `-mcrt=clib2` while `LDFLAGS` had it. `-mcrt` selects the
  runtime's *headers* as well as its libraries, so objects were compiled
  against newlib and linked against clib2. It only surfaces when a translation
  unit touches a newlib-specific inline, e.g. `_impure_ptr` (doslistdiag) or
  `__locale_ctype_ptr` (fboot).
