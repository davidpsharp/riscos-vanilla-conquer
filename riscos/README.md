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
| `scripts/fetch-runtime.sh` | Download the SharedULib and DRenderer modules into `~/vcport-work/hostfs/modules` |
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
mkdir -p ~/vcport-work/hostfs/tests && cp build/riscos-tests/*,ff8 riscos/tests/runsdl,feb ~/vcport-work/hostfs/tests/

RPCEMU=/Applications/RPCEmu.app/Contents/MacOS
$RPCEMU/rpcemu --machine "VC SA RO371" --headless &      # VNC 5920, HostCmd 5921
$RPCEMU/rpcemu-run --tcp 127.0.0.1:5921 -- 'RMEnsure SharedUnixLibrary 1.16 RMLoad HostFS:$.vc.modules.SharedULib'
$RPCEMU/rpcemu-run --tcp 127.0.0.1:5921 -- 'HostFS:$.vc.tests.hello'
$RPCEMU/rpcemu-run --tcp 127.0.0.1:5921 -- 'Filer_Run HostFS:$.vc.tests.runsdl'   # desktop programs
riscos/scripts/screenshot.sh 5920 build/shot.png
```

`HostFS:$.vc` in each machine points at `~/vcport-work/hostfs`.

## Findings so far

- GCCSDK 4.7.4 static binaries run on both RISC OS 3.71 and 5.30 once SharedUnixLibrary 1.16 is loaded.
- An unaligned `LDR` on StrongARM returns the word **rotated** and raises no fault, and RPCEmu does the same. Misaligned accesses therefore corrupt data silently. Find them with the macOS UBSan build (`-DBUILD_WITH_UBSAN=ON`).
- GCCSDK's libstdc++ is built without `_GLIBCXX_USE_C99`. `compat/cxx11_compat.h` covers the gap.
- `sdltest` in RPCEmu, 640×480×8 full screen with a full software redraw every frame: 24.5 fps on RO 3.71 and 19.9 fps on RO 5.30.
- GCCSDK defaults to `-mstructure-size-boundary=32` (APCS), which pads every struct to a multiple of 4 bytes. The game depends on x86 struct sizes, so the toolchain sets `=8`. Without it, GDI 1 crashed within seconds with a zeroed object.
- The RISC OS SDL 1.2 port queues a mouse motion event on every pump. `WWKeyboardClassSDL1::Fill_Buffer_From_System` therefore pumps once and then drains the queue.
- RISC OS SDL switches to relative mouse mode when the cursor is hidden in fullscreen, so on RISC OS the game uses a transparent cursor instead.
- UnixLib's `fnmatch` ignores `FNM_CASEFOLD`, so `Find_First` folds case itself.
- The SDL1 `To_ASCII` indexed an SDL2 scancode table with SDL1 key symbols. Typed text was garbage on every SDL1 platform.

## Playtest status (RISC OS 3.71, StrongARM, RPCEmu)

Tested on `VC SA RO371` (2 MB VRAM, 128 MB RAM) and `VC SA RO371 1MB` (1 MB VRAM, 64 MB RAM).

**Works on both:**
- intro/logo movies and the main menu
- GDI 1 via `-AUTOSTART=G1`
- selecting units, box-select, move and attack orders, deploying the MCV
- building and placing a Power Plant
- edge scrolling, the Esc options menu
- text entry, and Save/Load (including a save made on the other machine)

**Speed:** 15.5 logic fps at normal speed, the 15 fps target. Uncapped (`GameSpeed=0`) it reaches 119 fps, which is the 120 fps frame limiter. RPCEmu's dynarec is faster than a real StrongARM, so real-hardware speed is still to be measured.

**Video:** the game uses a 640×400 8bpp mode (250 KB), which fits either VRAM size.

**Not yet done:** sound (Phase 3). RISC OS 3.7 FileCore's 10-character filename limit also needs handling for game data on a real disc; HostFS doesn't have the limit.

## Day-to-day loop

```sh
VC_DEBUG=1 riscos/scripts/deploy-td.sh        # cross-build (with debug log) and deploy
riscos/scripts/run-td.sh 371 -AUTOSTART=G1    # reset machine, launch TD
riscos/scripts/screenshot.sh 5920 /tmp/s.png  # look at it
tail ~/vcport-work/hostfs/'!VanillaTD'/stderr # debug log (flushed in 1 KB blocks)
```

When debugging, `rpcemu-debug trace config data_abort=1 prefetch_abort=1 undefined=1` stops the emulator at the faulting instruction. `scripts/guest-stack.py` gives a heuristic backtrace. On the Mac, `scripts/run-mac.sh build/mac-ubsan -AUTOSTART=G1` runs the sanitizer build and writes reports to `~/vcport-work/ubsan.*`.

Game data, the HostFS deploy directory and the Mac run directory all live in `~/vcport-work`. They stay out of the Synology-synced source tree, which otherwise uploads about 500 MB and has reverted local edits.
