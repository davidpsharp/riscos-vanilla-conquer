#!/usr/bin/env bash
# Build UnixLib's patched objects from jhamby's riscos-gccsdk at a pinned commit plus
# ticker-only.diff, and put them into the toolchain's static libunixlib.a.
# Run as root inside the cross-build image (riscos/docker/Dockerfile does); see README.md.
# Needs: network (GitHub, ftp.gnu.org), git, wget, perl, autoconf, make, patch,
# and the GCCSDK 4.7.4 arm-unknown-riscos tools on PATH.
set -euo pipefail

GCCSDK_URL=https://github.com/jhamby/riscos-gccsdk
GCCSDK_SHA=64c6f814eab25486685677b1c5b5ce3d8214d82f
GCC_URL=https://github.com/gcc-mirror/gcc
GCC_TAG=releases/gcc-10.2.0
GCC_SHA=ee5c3db6c5b2c3332912fb4c9cfa2864569ebd9a
AUTOMAKE_TAR_SHA=53dbf1945401c43f4ce19c1971baecdbf8bc32e0f37fa3f49fe7b6992d0d2030
# The objects the patch changes or adds (anything else in the library stays the stock one).
OBJECTS="_context.o _syslib.o atexit.o context.o exec.o pthinit.o ticker.o"

HERE=$(cd "$(dirname "$0")" && pwd)
LIB=/home/riscos/cross/arm-unknown-riscos/lib/libunixlib.a
W=$(mktemp -d /tmp/unixlib-patch.XXXXXX)
export PATH=$W/tools/bin:/home/riscos/cross/bin:$PATH
T=arm-unknown-riscos
cd "$W"

# 1. The UnixLib source, at the pinned commit (only its directory).
mkdir gccsdk && cd gccsdk
git init -q && git remote add origin "$GCCSDK_URL"
git config core.repositoryformatversion 1   # the image's old git wants this for --filter
git config extensions.partialClone origin
git sparse-checkout set gcc4/recipe/files/gcc/libunixlib
git fetch -q --depth 1 --filter=blob:none origin "$GCCSDK_SHA"
git checkout -q FETCH_HEAD
[ "$(git rev-parse HEAD)" = "$GCCSDK_SHA" ]
patch -p1 -s < "$HERE/ticker-only.diff"
SRC=$W/gccsdk/gcc4/recipe/files/gcc
cd "$W"

# 2. What UnixLib's autotools setup needs: GCC 10.2's top-level files and config directory
#    (the same files as in the gcc-10.2.0 release tarball) and automake 1.11.6 (newer automakes
#    reject its Makefile.am).
mkdir gccsrc && cd gccsrc
git init -q && git remote add origin "$GCC_URL"
git config core.repositoryformatversion 1
git config extensions.partialClone origin
git sparse-checkout init --cone   # cone mode: the top-level files as well as config/
git sparse-checkout set config
git fetch -q --depth 1 --filter=blob:none origin "refs/tags/$GCC_TAG"
git checkout -q FETCH_HEAD
[ "$(git rev-parse HEAD)" = "$GCC_SHA" ]
rm -rf .git
cd "$W"
wget -q https://ftp.gnu.org/gnu/automake/automake-1.11.6.tar.gz
echo "$AUTOMAKE_TAR_SHA  automake-1.11.6.tar.gz" | sha256sum -c -
mkdir am && tar xzf automake-1.11.6.tar.gz -C am
( cd am/automake-1.11.6 && ./configure --prefix="$W/tools" --program-suffix=-1.11 >/dev/null && make -j4 >/dev/null && make install >/dev/null )
mkdir -p "$W/tools/share/aclocal"   # aclocal-1.11 insists on its (empty) system directory

# 3. Build libunixlib.a (-O2 and the toolchain's default ARMv3 code, like the stock one).
mkdir -p out/src && cp gccsrc/* out/src/ 2>/dev/null || true
cp -r gccsrc/config out/src/config
( cd out/src && patch -p0 -s < "$HERE/libtool.m4.p" )
cp -r "$SRC/libunixlib" out/src/libunixlib
( cd out/src && ./libunixlib/gen-auto.pl )
( cd out/src/libunixlib && ACLOCAL="aclocal-1.11 -I .. -I ../config" AUTOMAKE=automake-1.11 LIBTOOLIZE=true autoreconf -f >/dev/null )
mkdir out/build && cd out/build
CC=$T-gcc AR=$T-ar RANLIB=$T-ranlib CFLAGS=-O2 ../src/libunixlib/configure --host=$T --target=$T \
    --build=x86_64-linux-gnu --prefix=/tmp/inst --disable-shared --enable-static --disable-multilib >configure.log 2>&1 \
    || { tail -30 configure.log; exit 1; }
make -j4 >make.log 2>&1 || { tail -40 make.log; exit 1; }

# 4. Put the changed objects into the toolchain's library (replaced in place; ticker.o is new).
[ -f "$LIB.stock" ] || cp "$LIB" "$LIB.stock"
mkdir objs && ( cd objs && $T-ar x ../.libs/libunixlib.a $OBJECTS )
# shellcheck disable=SC2086
( cd objs && $T-ar r "$LIB" $OBJECTS ) && $T-ranlib "$LIB"
printf 'libunixlib.a: stock GCCSDK 4.7.4 library with %s replaced by the build of\n%s at %s\nplus riscos/unixlib-patch/ticker-only.diff (thread ticker patch).\n' \
    "$OBJECTS" "$GCCSDK_URL" "$GCCSDK_SHA" > "$LIB.patched"
cd /
rm -rf "$W"
echo "patched $LIB"
