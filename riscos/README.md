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

### Campaign flow (RISC OS 3.71, 1 MB VRAM)

Tested using `-PLAYTEST` (Alt+W wins, Alt+L loses) and `-AUTOSTART=<G|N><n>[A-D]`:
- **Win GDI 1:** victory movie → score screen → Top Scores initials entry → campaign map → territory info → GDI 2 briefing → GDI 2, with credits carried over.
- **Lose GDI 2:** defeat movie → "replay this mission?" → Yes restarts GDI 2.
- **Winter theatre:** GDI 8 variant A renders correctly.
- **Stability:** a 10-minute session in GDI 8 held 15.5–15.6 logic fps and real-time audio every minute, with crash traps armed and none hit.
- **Load:** sending the army into battle stayed at 15.5 fps.

`scripts/start-mission.sh <machine> <vnc> <mission> [args]` starts a mission and skips the movies.

## Sound

There's no OpenAL on RISC OS, so SDL builds without OpenAL use a small software mixer on SDL 1.2's audio callback (`common/mixer_sdl1.cpp`). Its channels mimic the OpenAL sources that `soundio_common.cpp` and the VQA player expect. `soundio_sdl1.cpp` handles effects, music and speech, and `vqaaudio_sdl1.cpp` handles movie soundtracks. On RISC OS, SDL plays through **DigitalRenderer**, which `!Run` loads from `System:Modules` or from `!VanillaTD.Modules`. If audio can't be opened, the game runs silently.

**How it was verified:**
- **macOS:** `SDL_AUDIODRIVER=disk` writes the mixed output to a file. The logo, music, speech and effects all decode as real audio (zero-crossing rate 0.01–0.08). A capture is in `~/vcport-work/td-audio-mac.wav`.
- **RISC OS 3.71 and 5.30 (RPCEmu):** `scripts/capture-mixer.py` breaks on `Mixer_Callback` and reads back the buffer it filled. The output is real audio in both the logo movie and GDI 1. Playback is paced at 22009 and 22207 frames/s against 22050 expected (from `Mixer_Frames_Mixed`), so the sound hardware consumes it in real time.
- **Cost:** the game still runs at 15.6 logic fps. Uncapped throughput drops by about 5.5% (827 vs 876 fps), mostly from UnixLib's pthread audio thread rather than the mixing itself.

## Start-up and loading times

Measured on RPCEmu, StrongARM, RISC OS 3.71:

| | Time |
| --- | --- |
| Launch → main menu | ~27 s with the Westwood logo; 2.4 s with `PlayLogo=No` or Esc |
| Title screen → menu buttons | 1.2 s |
| Movie-less launch → GDI 1 playing | ~4 s |
| Loading a save, from the menu | 3.3 s |
| Loading a save, from inside a mission | 2.1 s |

Almost all of the remaining wait is the movies, which play in real time: the logo is 27.9 s and the GDI 1 briefing 36.6 s. Esc skips any movie. `PlayLogo=No` in the `[Intro]` section of `INI.CONQUER` skips the logo at start-up.

Why it's this fast: on RISC OS the game skips `Resolve_File`'s case-fixing directory scans, and `RISCOS_Fopen` remembers files that don't exist, so repeated probes are free. `scripts/profile.py` is a sampling profiler over the RPCEmu debugger, and `scripts/time-menu.sh` measures start-up.

## Installing the game data on RISC OS

The game data isn't redistributable, so `!VanillaTD` ships without it. `!VanillaTD.Prepare` installs it from the freeware C&C95 (C&C Gold) CD images, on the RISC OS machine itself. Run it once per disc:

```
*Obey <path>.!VanillaTD.Prepare <path>.CNC95_GDI/iso [-nomovies] [-force]
*Obey <path>.!VanillaTD.Prepare <path>.CNC95_Nod/iso [-nomovies]
```

It runs `Utils.vcprep` (source in `tools/vcprep`), which:
- reads the ISO image directly, with no CDFS or mounting needed;
- unpacks the files in `INSTALL/SETUP.Z` (InstallShield 3, PKWARE implode; decompressed with Mark Adler's `blast.c`);
- writes the short-filename layout below, with filetypes set;
- checks every file against a known CRC-32 and says which disc it is from `GENERAL.MIX`.

The shared files are identical on both discs, so the second run skips them, and it keeps an existing `INI.CONQUER`. `-nomovies` leaves out `MOVIES` (about 430 MB per disc). The DOS discs aren't supported. Other versions of the C&C95 discs would fail the checksums.

**Tested:**
- Natively on macOS against both ISOs, where its output matches `prepare-td-data.sh`.
- On RO 3.71 (RPCEmu): writing to ADFS, and through `Prepare` with both discs. GDI 1 and Nod 1 then start from the prepared app.


The GDI and Nod CDs differ only in `GENERAL.MIX` (scenarios and briefings) and `MOVIES.MIX`. `prepare-td-data.sh <GDI.iso> --movies --nod <Nod.iso>` stores those per disc. `deploy-td.sh` then lays the app out like this:

```
!VanillaTD.MIX.*               shared data (CONQUER, SCORES, SOUNDS, ...)
!VanillaTD.gdi.MIX.GENERAL     GDI disc (plus .MOVIES)
!VanillaTD.nod.MIX.GENERAL     Nod disc (plus .MOVIES)
```

The game picks `gdi` or `nod` according to the side being played (`Force_CD_Available`). `GENERAL` and `MOVIES` must not also be at the top level, or they would shadow the other side's copies.

**Tested on RO 3.71:**
- Nod mission 1 via `-AUTOSTART=N1`, and via Start New Game → Select Transmission → Nod, with the Nod briefing.
- Saving a Nod game, loading a GDI save from it, and loading the Nod save back.

The movies are about 430 MB per side and can be left out on small discs.

## Keys

SDL 1.2 numbers non-ASCII keys (modifiers, arrows, F keys, keypad) 256–322, which collided with the keyboard code's modifier bits. `SDL1_VK` folds them into 128–255.

**Tested on RO 3.71:**
- Shift, Ctrl and Alt are tracked correctly: Shift-click adds to the selection, Ctrl gives the force-attack cursor and Alt the force-move cursor.
- The F9–F12 bookmarks work: Ctrl/Shift/Alt+F9 sets one, F9 jumps back.
- Home works.

Hotkeys are stored in `[SDL1Hotkeys]`.

## Filenames (10-character FileCore)

RISC OS 3.x FileCore allows 10-character leaf names and silently truncates longer ones. UnixLib stores `TEMPICNH.MIX` as `TEMPICNH/MIX` (12 characters), which becomes `TEMPICNH/M`. So on RISC OS, `NAME.EXT` files are kept the traditional way, in an `EXT` directory:

```
!VanillaTD.!Run            !VanillaTD.MIX.CONQUER     (CONQUER.MIX)
!VanillaTD.!RunImage       !VanillaTD.MIX.TEMPICNH    (TEMPICNH.MIX)
!VanillaTD.INI.CONQUER     (CONQUER.INI)
!VanillaTD.000.SAVEGAME    (SAVEGAME.000, created by the game)
```

- **Reading:** the game tries the short form first, then the plain name, so data copied with long names still works on HostFS or a long-filename disc.
- **Writing:** the extension directory is created when needed. See `common/riscos_fs.h`.
- **Directory scans:** `Find_First` searches both layouts.
- **Deploying:** `deploy-td.sh` lays the data out this way and warns about any name over 10 characters.
- **Testing on the Mac:** build with `-DSHORT_FILENAMES=ON`, and set `VC_FSLOG=1` to trace file opens.

This was verified by copying the app (without `MIX.MOVIES`, since the 256 MB HardDisc4 can't hold it) to `ADFS::HardDisc4.$` on RISC OS 3.71. From there, GDI 1, Save and Load all work.

Without `MIX.MOVIES` the game just skips the movies, so it can be left out on small discs. SCORES (37 MB) holds the music, which isn't used until sound is implemented.

## Day-to-day loop

```sh
VC_DEBUG=1 riscos/scripts/deploy-td.sh        # cross-build (with debug log) and deploy
riscos/scripts/run-td.sh 371 -AUTOSTART=G1    # reset machine, launch TD
riscos/scripts/screenshot.sh 5920 /tmp/s.png  # look at it
tail ~/vcport-work/hostfs/'!VanillaTD'/stderr # debug log (flushed in 1 KB blocks)
```

When debugging, `rpcemu-debug trace config data_abort=1 prefetch_abort=1 undefined=1` stops the emulator at the faulting instruction. `scripts/guest-stack.py` gives a heuristic backtrace. On the Mac, `scripts/run-mac.sh build/mac-ubsan -AUTOSTART=G1` runs the sanitizer build and writes reports to `~/vcport-work/ubsan.*`.

Game data, the HostFS deploy directory and the Mac run directory all live in `~/vcport-work`. They stay out of the Synology-synced source tree, which otherwise uploads about 500 MB and has reverted local edits.
