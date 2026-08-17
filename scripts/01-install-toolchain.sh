#!/usr/bin/env bash
# Build bebbo's m68k-amigaos cross toolchain on macOS (Apple Silicon or Intel).
#
# The fujinet-nio workspace expects m68k-amigaos-gcc with clib2 support
# (see repos/nio-apps/makefiles/compiler-amigagcc.mk: -mcrt=clib2).
#
# Upstream default PREFIX is /opt/amiga, which needs root on macOS.  We install
# to $HOME/opt/amiga instead and point the workspace at it through
# local/config.env (AMIGA_TOOLCHAIN_BIN).  Override with PREFIX=... if you do
# want /opt/amiga and are willing to sudo mkdir/chown it first.
set -euo pipefail

PROJ="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX="${PREFIX:-$HOME/opt/amiga}"
SRC="${SRC:-$PROJ/toolchain-src}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu)}"
LOG="$PROJ/logs/toolchain-build.log"

BREW="$(brew --prefix)"

mkdir -p "$PROJ/logs" "$PREFIX"

echo "==> prefix : $PREFIX"
echo "==> source : $SRC"
echo "==> jobs   : $JOBS"
echo "==> log    : $LOG"

# Upstream moved off GitHub; Codeberg is the live remote.
if [ ! -d "$SRC/.git" ]; then
  git clone https://codeberg.org/bebbo/amiga-gcc.git "$SRC"
fi

# Homebrew bison/flex are keg-only but the build needs them ahead of the
# outdated macOS system copies.  gnu-sed is likewise required by the scripts.
export PATH="$BREW/opt/bison/bin:$BREW/opt/flex/bin:$BREW/opt/gnu-sed/libexec/gnubin:$PREFIX/bin:$PATH"

cd "$SRC"

# The Makefile's Darwin branch only passes --with-libgmp-prefix, which the
# binutils top level accepts but the bundled GDB configure does not: it looks
# for --with-gmp/--with-mpfr and otherwise fails with
#   "Building GDB requires GMP 4.2+, and MPFR 3.1.0+".
# Homebrew on Apple Silicon puts those under /opt/homebrew, which is not on
# configure's default search path.  Restate the full flag set with the GDB
# spellings added.
CONFIG_BINUTILS="--prefix=$PREFIX --target=m68k-amigaos --disable-werror"
CONFIG_BINUTILS="$CONFIG_BINUTILS --enable-tui --disable-nls --disable-plugins"
CONFIG_BINUTILS="$CONFIG_BINUTILS --with-libgmp-prefix=$BREW"
CONFIG_BINUTILS="$CONFIG_BINUTILS --with-gmp=$BREW --with-mpfr=$BREW --with-mpc=$BREW"

# GCC bundles a very old zlib whose zutil.h reads TARGET_OS_MAC as "classic
# Mac OS, which has no fdopen" and does `#define fdopen(fd,mode) NULL`.  The
# modern macOS SDK defines TARGET_OS_MAC=1, so that macro then mangles the
# real fdopen declaration in <stdio.h>:
#   _stdio.h:322:7: error: expected identifier or '('
# Build against the system zlib instead of the bundled copy.
PROJECTS_DIR="$SRC/projects"
CONFIG_GCC="--prefix=$PREFIX --target=m68k-amigaos"
CONFIG_GCC="$CONFIG_GCC --enable-languages=c,c++,objc"
CONFIG_GCC="$CONFIG_GCC --enable-version-specific-runtime-libs --disable-libssp --disable-nls"
CONFIG_GCC="$CONFIG_GCC --with-headers=$PROJECTS_DIR/newlib-cygwin/newlib/libc/sys/amigaos/include/"
CONFIG_GCC="$CONFIG_GCC --disable-shared --enable-threads=no"
CONFIG_GCC="$CONFIG_GCC --with-stage1-ldflags=-dynamic-libgcc\ -dynamic-libstdc++"
CONFIG_GCC="$CONFIG_GCC --with-boot-ldflags=-dynamic-libgcc\ -dynamic-libstdc++"
CONFIG_GCC="$CONFIG_GCC --with-gmp=$BREW --with-mpfr=$BREW --with-mpc=$BREW"
CONFIG_GCC="$CONFIG_GCC --with-system-zlib"

MAKE_COMMON=(PREFIX="$PREFIX" SHELL="$BREW/bin/bash"
             CONFIG_BINUTILS="$CONFIG_BINUTILS" CONFIG_GCC="$CONFIG_GCC")

# Fetch the GCC sources on their own first, so the clang fix below can be
# applied before anything compiles.  This target is just the cloned file.
gmake "$SRC/projects/gcc/configure" "${MAKE_COMMON[@]}" 2>&1 | tail -2

# GCC 6.5's fibonacci_heap.h line 481 says `heapb->min->compare (heapa->min)`
# where `min()` is a member *function* and `m_min` is the node pointer -- the
# very next line already assigns `heapa->m_min = heapb->m_min`.  GCC defers the
# lookup inside an uninstantiated template; clang does not, and fails with
#   error: reference to non-static member function must be called
# Fixed upstream in later GCC.  Apply the same one-line correction here.
FIBHEAP="$SRC/projects/gcc/gcc/fibonacci_heap.h"
if grep -q 'heapb->min->compare (heapa->min)' "$FIBHEAP"; then
  echo "==> patching $FIBHEAP (min -> m_min)"
  "$BREW/opt/gnu-sed/libexec/gnubin/sed" -i \
    's/heapb->min->compare (heapa->min)/heapb->m_min->compare (heapa->m_min)/' \
    "$FIBHEAP"
fi

# Upstream `all` is:
#   gcc binutils gdb gprof fd2sfd fd2pragma sfdc vasm libnix libgcc \
#   libpthread ndk ndk13 libSDL12 libnix4.library
#
# Two changes for this workspace:
#   + clib2  -- not in `all`, but nio-apps links with -mcrt=clib2
#               (repos/nio-apps/makefiles/compiler-amigagcc.mk)
#   - libSDL12 -- its Amiga SDL 1.2 timer backend fails to compile
#               ("conflicting types for 'TimerBase'" in
#               timer/amigaos/SDL_systimer.c) and nothing in fujinet-nio
#               uses SDL on the Amiga side.

# Same trick for clib2: fetch, then fix, then build.
gmake "$SRC/projects/clib2/LICENSE" "${MAKE_COMMON[@]}" 2>&1 | tail -2

# clib2's non-AmigaOS4 CONSTRUCTOR/DESTRUCTOR macros end with
#   VOID __ctor_##name##(VOID);
# The trailing `##` tries to paste an identifier onto `(`, which is not a valid
# preprocessing token.  Older compilers let it slide; GCC 6.5 rejects it with
#   error: pasting "__ctor_math_init" and "(" does not give a valid
#          preprocessing token
# The pasting is unnecessary -- `__ctor_##name(VOID)` is what was meant.
CTOR="$SRC/projects/clib2/library/stdlib_constructor.h"
if grep -q '##name##(VOID)' "$CTOR"; then
  echo "==> patching $CTOR (stray ## before '(')"
  "$BREW/opt/gnu-sed/libexec/gnubin/sed" -i 's/##name##(VOID)/##name(VOID)/g' "$CTOR"
fi

# clib2 aliases the POSIX field names onto its Amiga ones with
#   #define tv_sec   tv_secs
#   #define tv_usec  tv_micro
# because the old OS 3.x <devices/timer.h> only had tv_secs/tv_micro.  The
# NDK 3.2 shipped with amiga-gcc now supplies *both* spellings through
# anonymous unions:
#   union { ULONG tv_sec; ULONG tv_secs; };
# so the macro rewrites that union into two members called tv_secs and the
# build stops with
#   devices/timer.h:79: error: duplicate member 'tv_secs'
# Drop the aliases and give clib2's own structs the same dual-name unions,
# which keeps every existing spelling valid on both sides.
python3 - "$SRC/projects/clib2/library/include" <<'PY'
import sys
from pathlib import Path

inc = Path(sys.argv[1])
changed = []

alias_sec = """#ifndef tv_sec
#define tv_sec tv_secs
#endif /* tv_sec */
"""
alias_usec = """#ifndef tv_usec
#define tv_usec tv_micro
#endif /* tv_usec */
"""

t = (inc / "sys/time.h").read_text()
if alias_sec in t:
    t = t.replace("""struct timeval
{
\tunsigned long tv_secs;
\tunsigned long tv_micro;
};""", """struct timeval
{
\tunion {
\t\tunsigned long tv_sec;
\t\tunsigned long tv_secs;
\t};
\tunion {
\t\tunsigned long tv_usec;
\t\tunsigned long tv_micro;
\t};
};""")
    t = t.replace(alias_sec, "").replace(alias_usec, "")
    (inc / "sys/time.h").write_text(t)
    changed.append("sys/time.h")

t = (inc / "time.h").read_text()
if alias_sec in t:
    t = t.replace("""struct timespec
{
\ttime_t tv_secs;
\tlong tv_nsec;
};""", """struct timespec
{
\tunion {
\t\ttime_t tv_sec;
\t\ttime_t tv_secs;
\t};
\tlong tv_nsec;
};""")
    t = t.replace(alias_sec, "")
    (inc / "time.h").write_text(t)
    changed.append("time.h")

print("==> patched clib2 timeval/timespec:", ", ".join(changed) if changed else "already applied")
PY

#
# The targets are also split into two phases.  The host-side tools parallelise
# cleanly.  The m68k target libraries do not: GCC's recursive make for
# libgcc/libstdc++/libobjc/libquadmath races its own configure step under -j,
# and the build dies with
#   ./libtool: No such file or directory            (Error 127)
#   No rule to make target 'math/.deps/acoshq.Plo'
# because compilation starts before configure has written libtool and the
# dependency stubs.  Building that phase with -j1 is the reliable fix, and it
# is cheap next to the host compiler.
HOST_TARGETS=(gcc binutils gdb gprof fd2sfd fd2pragma sfdc vasm)
TARGET_LIBS=(libnix libgcc clib2 libpthread ndk ndk13 libnix4.library)

# `make` on macOS is GNU Make 3.81, which is known to produce link failures
# here; Homebrew's gmake is 4.x.  SHELL must also be the Homebrew bash.
gmake "${HOST_TARGETS[@]}" -j"$JOBS" \
  "${MAKE_COMMON[@]}" \
  2>&1 | tee "$LOG"

gmake "${TARGET_LIBS[@]}" -j1 \
  "${MAKE_COMMON[@]}" \
  2>&1 | tee -a "$LOG"

echo
echo "==> installed:"
"$PREFIX/bin/m68k-amigaos-gcc" --version | head -1
