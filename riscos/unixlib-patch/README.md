# UnixLib with the thread-ticker patch

The games are linked statically against GCCSDK 4.7.4's `libunixlib.a`. Stock UnixLib's pthread
ticker (the timer behind the threads, used by the audio thread) could crash the game when it quit:
a 100-cycle start-and-quit soak of Red Alert crashed 12 times in 100 with the stock library and
0 times in 100 with the patched one. So the cross-build image replaces seven objects of the
stock library with ones built from patched UnixLib source.

## Origin and terms

- `ticker-only.diff` is the thread-ticker part of the changes in
  <https://github.com/adyoull/riscos-unixlib>, an **unofficial fork** of GCCSDK's UnixLib (the
  OpenTS port ships its C library from there too). It is not part of GCCSDK or UnixLib releases.
  It is a patch against the UnixLib source in <https://github.com/jhamby/riscos-gccsdk> at commit
  `64c6f814eab25486685677b1c5b5ce3d8214d82f` (`gcc4/recipe/files/gcc/libunixlib`), cut down to the
  ticker (`pthread/ticker.c`, `internal/ticker.s`, the context switcher, `_syslib.s`, `pthinit.c`,
  `atexit.c`, `exec.c`, the `PThreadTicker` module source) and nothing else.
- UnixLib is BSD licensed with LGPL parts, and the LGPL is the overriding licence (the
  `Licences/UnixLib` file in the release zip says so). The patched objects are LGPL 2.1 or later
  like the code they patch. Because the games are statically linked, the LGPL needs the means to
  relink: this directory is the complete source of the change (the patch and the exact upstream
  commit), `riscos/docker/Dockerfile` the build, and the game's source is GPL-3 anyway.
- The patch is as written by the fork's author; it is only applied here, not edited.
  `libtool.m4.p` is the small patch to GCC 10's `libtool.m4` that UnixLib's autotools build needs
  for the `riscos` target (it comes from the same GCCSDK build recipe).

## What the build does

`build-unixlib.sh` (run by the Dockerfile as the last step of the image):

1. fetches `gcc4/recipe/files/gcc/libunixlib` of riscos-gccsdk at the pinned commit (a sparse,
   one-commit fetch) and applies `ticker-only.diff`;
2. fetches GCC 10.2.0's top-level files and `config` directory (tag `releases/gcc-10.2.0` of
   github.com/gcc-mirror/gcc, commit-checked) and automake 1.11.6 from ftp.gnu.org (SHA-256
   checked), and builds automake 1.11 (UnixLib's `Makefile.am` needs it);
3. builds `libunixlib.a` with the toolchain's compiler at `-O2` (the toolchain's default is ARMv3
   code, so no halfword or long-multiply instructions);
4. replaces `_context.o _syslib.o atexit.o context.o exec.o pthinit.o` in the toolchain's
   `/home/riscos/cross/arm-unknown-riscos/lib/libunixlib.a` and adds the new `ticker.o`
   (`objects-changed-by-patch.txt` is the list; every other object stays the stock one; the stock
   library is kept next to it as `libunixlib.a.stock`).

The image is `vc-riscos-sdk-ticker`; `riscos/scripts/sdk.sh` builds it on first use (needs network;
about 10 minutes on an Apple-silicon Mac under amd64 emulation). To rebuild just the library, delete
the image (`docker rmi vc-riscos-sdk-ticker`). To check an image:

```sh
riscos/scripts/sdk.sh cat /home/riscos/cross/arm-unknown-riscos/lib/libunixlib.a.patched
riscos/scripts/sdk.sh arm-unknown-riscos-nm /home/riscos/cross/arm-unknown-riscos/lib/libunixlib.a | grep -i ticker
```

The game binaries need rebuilding after the image changes (the library is linked in); a built game
contains the ticker code if `strings` finds `UnixLib$TickerStats` in it.

## Updating

To move to a newer patch or upstream commit: replace `ticker-only.diff` (a `git diff` against the
pinned commit, paths `a/gcc4/recipe/files/gcc/libunixlib/...`), change `GCCSDK_SHA` and the
`OBJECTS` list in `build-unixlib.sh` to the objects the new patch changes, and rebuild the image.
