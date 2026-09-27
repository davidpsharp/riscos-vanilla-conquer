# RISC OS port

Vanilla Conquer for RISC OS. The first target is a StrongARM Risc PC running RISC OS 3.7 or 4.x. The Raspberry Pi on RISC OS 5 comes later.

## Layout

| Path | Purpose |
| --- | --- |
| `docker/Dockerfile` | Cross-build image: GCCSDK 4.7.4 (`arm-unknown-riscos`), CMake, Ninja, SDL 1.2 |
| `cmake/riscos-gccsdk.cmake` | CMake toolchain file (`-mcpu=strongarm -fsigned-char`, static link, `riscos_make_aif()`) |
| `compat/cxx11_compat.h` | Force-included; supplies the `std::snprintf`, `std::stof` and `std::to_string` family that GCCSDK's libstdc++ lacks |
| `scripts/sdk.sh` | Run a command in the cross-build container with the repo at `/work` |
| `scripts/setup-rpcemu-machines.sh` | Create the `VC SA RO371` and `VC SA RO530` StrongARM machines in RPCEmu Extended |
| `scripts/fetch-runtime.sh` | Download the SharedULib and DRenderer modules into `build/hostfs/modules` |
| `scripts/screenshot.sh` | Grab a machine's screen as PNG over VNC |
| `tests/` | Toolchain smoke tests: `hello`, `unaligned`, `sdltest` |

## Host prerequisites (macOS)

```sh
brew install colima docker docker-buildx
colima start --vm-type vz --vz-rosetta   # the SDK image is linux/amd64
```

RPCEmu Extended must be installed at `/Applications/RPCEmu.app`, with its data directory at `~/rpcemu/rpcemu-extended`. The `RISC OS 3.71` and `RISC OS 5.30` template machines must exist. Create the second one with `rpcemu --fetch-riscos --accept-licence`.

## Quick start

```sh
riscos/scripts/setup-rpcemu-machines.sh
riscos/scripts/fetch-runtime.sh
riscos/scripts/build-tests.sh
mkdir -p build/hostfs/tests && cp build/riscos-tests/*,ff8 riscos/tests/runsdl,feb build/hostfs/tests/

RPCEMU=/Applications/RPCEmu.app/Contents/MacOS
$RPCEMU/rpcemu --machine "VC SA RO371" --headless &      # VNC 5920, HostCmd 5921
$RPCEMU/rpcemu-run --tcp 127.0.0.1:5921 -- 'RMEnsure SharedUnixLibrary 1.16 RMLoad HostFS:$.vc.modules.SharedULib'
$RPCEMU/rpcemu-run --tcp 127.0.0.1:5921 -- 'HostFS:$.vc.tests.hello'
$RPCEMU/rpcemu-run --tcp 127.0.0.1:5921 -- 'Filer_Run HostFS:$.vc.tests.runsdl'   # desktop programs
riscos/scripts/screenshot.sh 5920 build/shot.png
```

`HostFS:$.vc` in each machine points at `build/hostfs` in this repo.

## Findings so far

- GCCSDK 4.7.4 static binaries run on both RISC OS 3.71 and 5.30 once SharedUnixLibrary 1.16 is loaded.
- An unaligned `LDR` on StrongARM returns the word **rotated** and raises no fault, and RPCEmu does the same. Misaligned accesses therefore corrupt data silently. Find them with the macOS UBSan build (`-DBUILD_WITH_UBSAN=ON`).
- GCCSDK's libstdc++ is built without `_GLIBCXX_USE_C99`. `compat/cxx11_compat.h` covers the gap.
- `sdltest` in RPCEmu, 640×480×8 full screen with a full software redraw every frame: 24.5 fps on RO 3.71 and 19.9 fps on RO 5.30.
- GCCSDK defaults to `-mstructure-size-boundary=32` (APCS), which pads every struct to a multiple of 4 bytes. The game depends on x86 struct sizes, so the toolchain sets `=8`. Without it, GDI 1 crashed within seconds with a zeroed object.
- The RISC OS SDL 1.2 port queues a mouse motion event on every pump. `WWKeyboardClassSDL1::Fill_Buffer_From_System` therefore pumps once and then drains the queue.
- **Status:** TD starts GDI mission 1 (`-AUTOSTART=G1`) on StrongARM RISC OS 3.71 and 5.30 under RPCEmu. Sound is not implemented yet.

## Day-to-day loop

```sh
VC_DEBUG=1 riscos/scripts/deploy-td.sh        # cross-build (with debug log) and deploy
riscos/scripts/run-td.sh 371 -AUTOSTART=G1    # reset machine, launch TD
riscos/scripts/screenshot.sh 5920 /tmp/s.png  # look at it
tail ~/vcport-work/hostfs/'!VanillaTD'/stderr # debug log (flushed in 1 KB blocks)
```

When debugging, `rpcemu-debug trace config data_abort=1 prefetch_abort=1 undefined=1` stops the emulator at the faulting instruction. `scripts/guest-stack.py` gives a heuristic backtrace. On the Mac, `scripts/run-mac.sh build/mac-ubsan -AUTOSTART=G1` runs the sanitizer build and writes reports to `~/vcport-work/ubsan.*`.

Game data, the HostFS deploy directory and the Mac run directory all live in `~/vcport-work`. They stay out of the Synology-synced source tree, which otherwise uploads about 500 MB and has reverted local edits.
