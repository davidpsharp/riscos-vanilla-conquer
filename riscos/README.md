# RISC OS port

Vanilla Conquer for RISC OS: Tiberian Dawn (`!VanillaTD`) and Red Alert (`!VanillaRA`). The first target is a StrongARM Risc PC running RISC OS 3.7 or 4.x. The same binaries also run on a Raspberry Pi 4 with RISC OS 5.30.

## Layout

| Path | Purpose |
| --- | --- |
| `docker/Dockerfile` | Cross-build image: GCCSDK 4.7.4 (`arm-unknown-riscos`), CMake, Ninja, SDL 1.2 |
| `cmake/riscos-gccsdk.cmake` | CMake toolchain file (`-march=armv3 -mtune=strongarm -fsigned-char`, static link, `riscos_make_aif()`) |
| `compat/cxx11_compat.h` | Force-included; supplies the `std::snprintf`, `std::stof` and `std::to_string` family that GCCSDK's libstdc++ lacks |
| `scripts/sdk.sh` | Run a command in the cross-build container with the repo at `/work` |
| `scripts/setup-rpcemu-machines.sh` | Create the `VC SA RO371` and `VC SA RO530` StrongARM machines in RPCEmu Extended |
| `scripts/fetch-runtime.sh` | Download the SharedULib and DRenderer modules into `../vcport-work/hostfs/modules` |
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
mkdir -p ../vcport-work/hostfs/tests && cp build/riscos-tests/*,ff8 riscos/tests/runsdl,feb ../vcport-work/hostfs/tests/

RPCEMU=/Applications/RPCEmu.app/Contents/MacOS
$RPCEMU/rpcemu --machine "VC SA RO371" --headless &      # VNC 5920, HostCmd 5921
$RPCEMU/rpcemu-run --tcp 127.0.0.1:5921 -- 'RMEnsure SharedUnixLibrary 1.16 RMLoad HostFS:$.vc.modules.SharedULib'
$RPCEMU/rpcemu-run --tcp 127.0.0.1:5921 -- 'HostFS:$.vc.tests.hello'
$RPCEMU/rpcemu-run --tcp 127.0.0.1:5921 -- 'Filer_Run HostFS:$.vc.tests.runsdl'   # desktop programs
riscos/scripts/screenshot.sh 5920 build/shot.png
```

`HostFS:$.vc` in each machine points at `../vcport-work/hostfs`.

## Findings so far

- GCCSDK 4.7.4 static binaries run on both RISC OS 3.71 and 5.30 once SharedUnixLibrary 1.16 is loaded.
- An unaligned `LDR` on StrongARM returns the word **rotated** and raises no fault, and RPCEmu does the same. Misaligned accesses therefore corrupt data silently. Find them with the macOS UBSan build (`-DBUILD_WITH_UBSAN=ON`).
- GCCSDK's libstdc++ is built without `_GLIBCXX_USE_C99`. `compat/cxx11_compat.h` covers the gap.
- `sdltest` in RPCEmu, 640×480×8 full screen with a full software redraw every frame: 24.5 fps on RO 3.71 and 19.9 fps on RO 5.30.
- **No halfword instructions on a real Risc PC.** On a real Kinetic StrongARM Risc PC, `STRH` to heap memory left the upper byte unwritten. The keyboard queue then returned mouse clicks with garbage in the high byte, so most clicks were ignored. RPCEmu emulates `LDRH`/`STRH` correctly, so only real hardware showed it. The toolchain builds for ARMv3 (`-march=armv3 -mtune=strongarm`), like GCCSDK's own libraries, so no `LDRH`/`STRH` is generated (and no long multiply: see [ARMv3 CPUs](#armv3-cpus-arm610-arm710-arm7500)). Check with `scripts/check-armv3.sh <elf>`; `make-release.sh` runs it on everything it ships.
- GCCSDK defaults to `-mstructure-size-boundary=32` (APCS), which pads every struct to a multiple of 4 bytes. The game depends on x86 struct sizes, so the toolchain sets `=8`. Without it, GDI 1 crashed within seconds with a zeroed object.
- The RISC OS SDL 1.2 port queues a mouse motion event on every pump. `WWKeyboardClassSDL1::Fill_Buffer_From_System` therefore pumps once and then drains the queue.
- RISC OS SDL switches to relative mouse mode when the cursor is hidden in fullscreen, so on RISC OS the game uses a transparent cursor instead.
- UnixLib's `fnmatch` ignores `FNM_CASEFOLD`, so `Find_First` folds case itself.
- The SDL1 `To_ASCII` indexed an SDL2 scancode table with SDL1 key symbols. Typed text was garbage on every SDL1 platform.

## Real hardware

**Kinetic StrongARM Risc PC, 2 MB VRAM:** GDI mission 1 played through to the end (build `e9b66f3`, the first built without halfword instructions). The monitor has no 640×400 mode, so the game runs centred in 640×480. Measured with `VC_FPSLOG`: 26–30 frames presented per second, 13.4–14.2 game logic fps against a target of 15, and about 10–16 ms per full-screen copy.

**After the presentation and pacing fixes** (build `760a4d6`, same machine, in play): game logic 14.5–15.0 fps, 30 frames presented per second, the hidden-to-visible page copy down from ~250–300 to 1–2 ms/s, and about half of every second idle.

**Movies:** they play smoothly with sound in `InterpolationMode=1`, the RISC OS default. That mode blends horizontally and doubles lines. In mode 2 (vertical blending too, the desktop default), the GDI 1 briefing took 33–37 ms to scale plus 15 ms to copy to the screen, against a 67 ms frame budget, so it lagged with sound on. Mode 3 doubles pixels with no blending. `VC_FPSLOG` prints `movie:` lines with the per-frame scale and present times. The VQA audio buffer was also raised from 1.5 s to 3 s, because the briefing stores its audio further ahead of the video than that.

**Where the time goes:** with `VC_FPSLOG` set, the game also prints a `time ms/s:` line every 5 seconds, from `common/phasetime.*`. It gives milliseconds per second of wall time for:
- map, radar, power bar, sidebar and other drawing;
- game logic, event queue, `Call_Back`, presenting and sleeping;
- audio mixing, which runs on its own thread and overlaps the rest.

The frame limiter's sleep ends on a centisecond tick, so the centisecond clock would count most short work as zero. On RISC OS 3.5–4.x the timer instead reads IOMD timer 0 (2 MHz) in SVC mode. Elsewhere it uses `steady_clock`, which is coarse on RISC OS 5. RPCEmu is much faster than a real StrongARM, so only real-hardware numbers show what to optimise.

**What the timings led to** (StrongARM Risc PC, in play). At first, whole-screen copies took about 60% of every second: ~250 ms/s in `Blit_Display` (hidden page to visible page) and ~370 ms/s presenting (visible page to screen). The game logic ran at 12.4 of its 15 frames a second.
- **Unchanged frames aren't copied:** the game presents about twice per frame it draws, so presents copy only the cursor's old and new areas, or nothing, when nothing has drawn on the visible page. `VC_FULLPRESENT` turns this off.
- **The hidden page is presented directly:** `Blit_Display` makes the visible surface mirror the hidden one, instead of copying it (`video_sdl1.cpp`). The visible surface is filled in only when something actually uses it, such as a dialog. `VC_NODEFERBLIT` turns this off.
- **Pacing:** waiting for the next game frame no longer oversleeps it. The old sleeps ran to the next present slot, so each frame took three slots instead of two, which is why the game ran at 12.4 fps. `VC_SIMLOAD=<ms>` adds that much work per game frame, to reproduce a slow machine on RPCEmu. At 55 ms it gave 12.3–12.5 fps before the fix and 15.2–15.6 after.

## ARMv3 CPUs (ARM610, ARM710, ARM7500)

The games and their tools are built for ARMv3, so the same `!RunImage` runs on a StrongARM, an ARM610 or ARM710 Risc PC, an A7000 (ARM7500) and a Raspberry Pi.

**Why:** `-march=armv3m` (what the toolchain used before) lets GCC use the long multiplies `UMULL`, `SMULL`, `UMLAL` and `SMLAL`: 255 of them in TD and 368 in RA. They come from dividing by a constant (GCC multiplies by a reciprocal and keeps the high word), from 64-bit fixed-point maths, and from the sound mixer. The ARM610, ARM710 and ARM7500 don't have them, and they don't trap there either: RPCEmu's ARM710 runs `UMULL` as an `ADD` with a register shift (the encoding falls in the data-processing space), so the results are just wrong. On RPCEmu set up as an ARM710 with RISC OS 3.71, the armv3m TD hung the machine between the title and the game (stuck at the prefetch abort vector), and RA stopped with `Alloc: 2147483578 bytes not available` in `Get_Radar_Icon`. The armv3 builds of the same source played the battle benchmark and Allied 1 to the end, ending exactly as on a StrongARM. TD was also started from the icon bar, through the menus, into GDI 1. (RPCEmu's timings for an ARM710 mean nothing; a real ARM610/710 will be several times slower than a StrongARM, so 320x200 is the size to use there.)

**What ARMv3 costs:** without the long multiplies, GCC calls `__divsi3` to divide by a constant and `__muldi3` for a 64-bit multiply. Counting those calls by call site (a build with `-Wl,--wrap` wrappers, run on a Pi 3, which plays the benchmarks exactly as a Risc PC does) found the busy ones:

| Calls per game frame (RA battle) | armv3, as is | after the fixes |
| --- | --- | --- |
| `__muldi3` in the sound mixer (a sample times its gain, per sample) | 1,820 | 0 |
| `__divsi3` in `Coord_To_Pixel` (four `Pixel_To_Lepton`: divide by 24) | 4,080 | 0 |
| `__divsi3` in `Coord_Spillage_List` (`Pixel_To_Lepton` again) | 950 | 0 |
| the rest (`Shape_Number`, `Sort_Y`, `Desired_Facing256`, ...) | about 400 | about 400 |

The mixer now multiplies in 32 bits (a 16-bit sample times a gain of at most 0x10000 fits), and `Pixel_To_Lepton` multiplies by a reciprocal for pixels below 3072 (exact: checked against the division for every pixel from -2^23 to 2^23), in both games. Neither changes any result: all three builds end the benchmarks the same way.

Raspberry Pi 3, RISC OS 5.30, ms per game frame, 2 to 4 runs each (`vcport-work/armv3/armv3-bench.sh`):

| | armv3m | armv3 | armv3 with the fixes |
| --- | --- | --- | --- |
| TD battle | 5.21 | 5.30 (+1.6%) | 5.29 (+1.4%) |
| TD battle, no sound | 4.95 | 5.00 (+1.0%) | 5.00 (+0.9%) |
| RA battle | 15.07 | 15.54 (+3.1%) | 15.19 (+0.7%) |
| RA battle, no sound | 14.28 | 14.69 (+2.9%) | 14.31 (+0.2%) |
| RA Allied 1 | 5.28 | 5.34 (+1.2%) | 5.28 (0.0%) |
| RA Allied 2 | 5.70 | 5.79 (+1.6%) | 5.72 (+0.4%) |

TD's remaining 1.5% is in drawing, and isn't in the helper calls (about 200 a frame are left): apart from the functions that had long multiplies, the code is the same, so it is probably where the code now falls in the cache. The StrongARM Risc PC's figures are still to be measured (it was off): `vcport-work/armv3/README.md` has the builds and the one command that does it.

**If one binary ever costs too much on a StrongARM:** two `!RunImage`s (armv3m and armv3) would each add about 0.9 MB (TD) or 1.2 MB (RA) to the zip, which is 1.1 MB and 1.5 MB now. `tools/cpuprobe.s` is a 436-byte absolute for `!Run` that sets `VanillaConquer$LongMul` to 1 only if `UMULL` gives the right answer, and to 0 if it gives a wrong one (RPCEmu's ARM610/710) or traps (it catches the undefined instruction with its own error handler). Tested on RISC OS 3.11 (ARM3, Arculator), 3.71 (ARM710, RPCEmu), 4.39 (StrongARM, RPCEmu) and 5.30 (Pi 3).

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

There's no OpenAL on RISC OS, so SDL builds without OpenAL use a small software mixer on SDL 1.2's audio callback (`common/mixer_sdl1.cpp`). Its channels mimic the OpenAL sources that `soundio_common.cpp` and the VQA player expect. `soundio_sdl1.cpp` handles effects, music and speech, and `vqaaudio_sdl1.cpp` handles movie soundtracks. On RISC OS, SDL plays through **DigitalRenderer**, which `!Run` loads from `System:Modules` or from `!VanillaTD.Modules`. If audio can't be opened, the game runs silently. With `VanillaTD$Sound` (or `VanillaRA$Sound`) set to `SharedSoundBuffer` in `!Run`, the mixer instead feeds SharedSoundBuffer and StreamManager (John Duffell's freeware, over SharedSound, not shipped) from a thread of its own (`common/riscos_ssb.cpp`), so the sound mixes with other programs'. It falls back to SDL if the modules aren't loaded. On RISC OS 4.39 in RPCEmu, SharedSoundBuffer 0.07 holds only one queued block when a usual block size is given, so none is. And while any program plays through DigitalRenderer, SharedSound's streams are silent.

**How it was verified:**
- **macOS:** `SDL_AUDIODRIVER=disk` writes the mixed output to a file. The logo, music, speech and effects all decode as real audio (zero-crossing rate 0.01–0.08). A capture is in `../vcport-work/td-audio-mac.wav`.
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

The game data isn't redistributable, so `!VanillaTD` ships without it. It's installed from the freeware C&C95 (C&C Gold) CD images on the RISC OS machine itself, by dragging the ISO files onto the game's icon-bar icon (`common/riscos_desktop.cpp`). The game identifies each file from its ISO 9660 directory (`riscos/tools/vcprep/discid.h`, shared with the OpenTS port, which knows all seven freeware discs and the MS-DOS C&C ones) and runs `Utils.vcprep -window` in a task window, which only opens if something goes wrong. From the command line:

```
*Run <VanillaTD$Dir>.Utils.vcprep -app <VanillaTD$Dir> [<CD image>...] [-nomovies] [-force]
```

It runs `!VanillaTD.Utils.vcprep` (source in `tools/vcprep`), which:
- finds the images by their contents (an ISO 9660 volume holding the C&C95 `GENERAL.MIX`), not by name, since RISC OS 3.x cuts `CNC95_GDI/iso` down to 10 characters;
- reads the ISO image directly, with no CDFS or mounting needed;
- unpacks the files in `INSTALL/SETUP.Z` (InstallShield 3, PKWARE implode; decompressed with Mark Adler's `blast.c`);
- writes the short-filename layout below, with filetypes set;
- checks every file against a known CRC-32 and says which disc it is from `GENERAL.MIX`.

**Licences:** `vcprep.c` is GPL v3, like the rest of the project. `blast.c`/`blast.h` are Mark Adler's, under the zlib licence, and are included unmodified. The `SETUP.Z` layout follows [unshieldv3](https://github.com/wfr/unshieldv3) (Apache 2.0), used only as a format reference. All of these are compatible with GPL v3.

The shared files are identical on both discs, so they're written once, and it keeps an existing `INI.CONQUER`. `-nomovies` leaves out `MOVIES` (about 430 MB per disc). The DOS and Mac discs aren't supported: the game needs the Windows 95 data, and Prepare rejects the others. `!Help` lists download sites as of September 2026. Checked on 2026-09-29: the Windows 95 GDI and Nod images from cnc-comm.com (`CNC95_GDI.zip`, `CNC95_Nod.zip`) and CNCNZ.com (`GDI95.zip`, `NOD95.zip`, a different mastering) all pass every checksum, movies included. ModDB refuses automated downloads, so its copies are unchecked.

**Tested:**
- Natively on macOS against both ISOs, where its output matches `prepare-td-data.sh`.
- On RO 3.71 (RPCEmu): writing to ADFS, and through `Prepare` with both discs. GDI 1 and Nod 1 then start from the prepared app.


The GDI and Nod CDs differ only in `GENERAL.MIX` (scenarios and briefings) and `MOVIES.MIX`. `prepare-td-data.sh <GDI.iso> --movies --nod <Nod.iso>` stores those per disc. `deploy-td.sh` then lays the app out like this:

```
!VanillaTD.gamedata.MIX.*      shared data (CONQUER, SCORES, SOUNDS, ...)
!VanillaTD.gamedata.gdi.MIX.GENERAL  GDI disc (plus .MOVIES)
!VanillaTD.gamedata.nod.MIX.GENERAL  Nod disc (plus .MOVIES)
```

The game picks `gdi` or `nod` according to the side being played (`Force_CD_Available`). `GENERAL` and `MOVIES` must not also be at the top level, or they would shadow the other side's copies.

**Tested on RO 3.71:**
- Nod mission 1 via `-AUTOSTART=N1`, and via Start New Game → Select Transmission → Nod, with the Nod briefing.
- Saving a Nod game, loading a GDI save from it, and loading the Nod save back.

The movies are about 430 MB per side and can be left out on small discs.

## Red Alert

`deploy-ra.sh` builds `!VanillaRA` (`build/riscos-ra`, `-DBUILD_VANILLARA=ON`) with `Utils.vcprep` and `Utils.ramix`, and lays it out in `../vcport-work/hostfs/!VanillaRA`, hard-linking the data from `../vcport-work/ra` (the Allied and Soviet discs extracted to `CD1_ALLIES` and `CD2_Soviet`):

```
!VanillaRA.gamedata.MIX.REDALERT          the same on both discs
!VanillaRA.gamedata.allied.MIX.MAIN       the Allied disc's MAIN.MIX (455 MB with movies)
!VanillaRA.gamedata.soviet.MIX.MAIN       the Soviet disc's (500 MB)
!VanillaRA.gamedata.MIX.MAIN              or instead: MAIN.MIX without movies (85 MB, the same from either disc)
```

The game looks for a disc's files in `allied` or `soviet`, as it would ask for that CD. Without movies, one `MIX.MAIN` serves both campaigns.

**Memory:** the game needs more than RISC OS 3.x's 28 MB WimpSlot limit, so `startup.cpp` gives UnixLib a dynamic area for the heap (`__dynamic_da_name`, "VanillaRA heap", up to 64 MB). `!Run` sets a 6000K slot. Allied 1 uses 32.7 MB of heap.

**Prepare:** `vcprep` recognises a Red Alert disc image by its `MAIN.MIX` (Allied 454,605,294 bytes, Soviet 500,577,414) and `INSTALL/REDALERT.MIX`, and checks each by CRC-32. With movies it copies `MAIN.MIX` into `allied` or `soviet`. With `-nomovies` it runs `Utils.ramix copy <image>@<offset> <app>.gamedata.MIX.MAIN MOVIES1.MIX MOVIES2.MIX`, which decrypts the mixfile index (RSA and Blowfish, with the game's public key) and writes a plain mixfile without the movies. The game reads plain mixfiles as well as encrypted ones. `Prepare` works with either app or both (`-app`, `-raapp`, `VanillaTD$Dir`, `VanillaRA$Dir`). **Tested:** on the Mac, and on RO 3.71 (RPCEmu, HostFS) with the Soviet image and `-nomovies`; the game then ran from the prepared app. The Pi 4's SD card lacked the space for a 660 MB image.

**Low resolution:** `REDALERT.MIX` has the DOS version's 320x200 graphics (`LORES.MIX`), so `VanillaRA$LowRes` / `-LOWRES` needs nothing extra. On the Risc PC, Allied 1 took 10.3 ms/frame rather than 24.9.

**Benchmarks:** `BENCH_GAME=ra rorth-bench.sh <frames> <seed>` runs the battle from `bench/make-ra-battle.py` (Allied 1's map with 66 tanks and 66 soldiers a side), and `BENCH_MISSION=A1` (or `S1`) plays the mission itself. Results:

| | Risc PC (StrongARM) | Pi 4 |
| --- | --- | --- |
| battle, at the start of the night's work | 123.9 ms/frame | 4.85 |
| battle, now | 109.3 (logic 66.9, paths 16.8, draw 23.7, present 10.6) | 4.33 |
| battle with `VC_PARTIALPRESENT=1` | 107.5 (present 6.7) | |
| Allied 1 | 21.5 | 0.95 |
| Allied 1 with `VC_PARTIALPRESENT=1` | 13.2 (present 10.4 to 1.9) | |
| Allied 2 | 28.5 | 1.12 |
| Allied 2 with `VC_PARTIALPRESENT=1` | 23.5 | |
| start-up to the first frame | 14.9 s, now 12.8 s | 2.2 s |

**Missions:** Allied and Soviet 1–7 (3000 frames each, or until lost; with no player, Allied 4 and Soviet 7 are lost) run to the end on both machines, with the same outcomes. They take 18–29 ms/frame on the Risc PC. The game was also started normally there, and played through the menus into Allied 1 using the mouse.

**Start-up on the Risc PC:** 9 s of the 12.8 s is reading the cached mixfiles (LOCAL, HIRES, CONQUER, SOUNDS, the side and theater mixfiles: 14 MB) at the IDE disc's 2.4 MB/s. `VC_FPSLOG` logs each step (`Phase_Startup`). Checking their SHA-1 digests took 4.2 s before the SHA engine was unrolled, and now takes 2.0 s.

**Movie sound:** on the real Risc PC the 640x400 intro crashed when it played with its sound. With sound, movies are timed by the audio, so a slow machine skips frames to keep up, and the skipping exposed two bugs in the shared movie player. `VQA_DrawFrame_Buffer` drew the frame node it had fetched before `VQA_SelectFrame` skipped ahead: that frame's pointers had never been unpacked, so it read codebook entries far beyond the codebook (and stepped the drawer back). And Red Alert's codebooks have no LCW end code, so unpacking them ran on past the data and the buffer. Both are fixed (`LCW_Uncompress` with a source length), and all movies have sound again; found by hashing each frame's data on the Risc PC and on the Mac and comparing.

The Risc PC, the Pi 4 and the Mac end the battle the same way with the same seed. RPCEmu plays it differently, but the same each time.

**Speed-ups so far:**
- Paths:
  - A per-search cache of `Can_Enter_Cell`, shared by `Basic_Path`'s tries at rising thresholds.
  - One byte a cell (16 KB), so a look-up costs at most one cache miss.
  - `Passable_Cell`'s common case inline.
- `Nearby_Location` bounded by the map edge and by each zone's bounding box.
- Region threat kept per owner and added up when a house reads it, rather than written to 19 houses' regions for every cell a unit enters. A grand total makes reads quick: the first version summed 20 owners on every read and made roundabout teams' searches take 100 ms in Allied 2. Checked against the old way for every house and region every frame.
- Partial presents work in every mission: the empty map, objects and power bar passes now mark that they drew nothing (0 tiles missed with `VC_DIRTYCHECK`).
- `As_Pointer` cached.

## Screen size

**The icon bar menu:** Info, Screen size and Quit. Screen size lists Automatic (the size picked below, shown in brackets), the usual sizes (640x400, 640x480, 800x600, 1024x768, 1280x800, 1280x1024) that SDL lists for the monitor at 256 colours, any size set by hand in the settings file, and 320x200 (low resolution, `[Video] LowRes`). The start-up wait for a click (`RISCOS_Desktop_Start`) now comes before the game reads its screen size, so a size chosen there applies straight away. Chosen once the game is running (after Shift+F12), a different size asks first (Wimp_ReportError with Restart and Cancel), then saves it, sets `VanillaTD$Restart` and quits; `Filer_Run` of the application, queued just before, starts it again after the old copy has gone, and the new copy skips the click. `VanillaTD$LowRes=1` still forces 320x200, and then the menu's other sizes are shaded.

**Automatic:** `[Video] Resolution` is `auto` by default on RISC OS (`common/riscos_autosize.cpp`). The first time, a speed test (`common/riscos_speedtest.cpp`, about 0.8 s, before the icon appears) measures a logic-like loop, memory copies, and writes to screen memory (the desktop's own pixels, read once and written back, so nothing changes on screen). The results go in the settings file's `[RISCOS]` section; later starts only run a tenth of a second of the loop, to notice a copy moved to another machine, and pick again for the desktop as it is. `VC_RETUNE=1` measures again. The pick is the largest size in the desktop's shape (and no bigger than it) whose projected load, 15 game frames of logic and drawing plus FrameLimit full-screen presents a second, scaled from the Risc PC's measured times, stays under 600 ms a second; an exact fraction of the desktop nearly as big is preferred, and 640x400 is the floor. `riscos/tests/speedtest` runs the test alone and prints the pick.

| Machine | cpu | memory (KB/s) | screen (KB/s) | Desktop | Pick (projected ms/s) |
| --- | --- | --- | --- | --- | --- |
| Risc PC, StrongARM, RISC OS 4.39 | 10,500 | 13,800–14,300 | 18,400 | 1280x1024, 8 bpp | 640x400, the floor (640x400 itself projects 684) |
| Raspberry Pi 4, RISC OS 5.30 | 90,300 | 2,609,000 | 2,387,000 | 1680x1050, 32 bpp | 1680x1050 (68 at 60 presents) |
| Raspberry Pi 3, RISC OS 5.30 | 46,600 | 728,000 | 822,000 | 1680x1050, 32 bpp | 1680x1050 (179 at 60 presents) |
| Raspberry Pi 2, RISC OS 5.30 | 30,900 | 309,000 | 561,000–566,000 | 1680x1050, 32 bpp | 1680x1050 (274 at 60 presents) |
| RPCEmu, RISC OS 5.30 (emu530) | 15,500–16,000 | 516,000–547,000 | 491,000–539,000 | 800x600, 8 bpp | 800x600 (180 at 60 presents); 1024x768 on a 1024x768 desktop (220) |
| RPCEmu, RISC OS 3.71 (emu371) | 15,700 | 242,000 | 252,000 | 800x600, 32 bpp | 800x600 (187 at 30 presents) |

## Network play

LAN play works: Multiplayer Game → Network. Vanilla Conquer emulates the original IPX networking over UDP broadcast. On RISC OS the game lists the network interfaces with `SIOCGIFCONF`, because UnixLib has no `getifaddrs`. It falls back to broadcasting to `255.255.255.255`. The machine needs a network card and the Internet module. Without them, Network just returns to the menu.

**Tested on RPCEmu:** a RISC OS 5.30 and a RISC OS 3.71 machine on RPCEmu's shared virtual LAN hosted, joined and played a 2-player game in sync. They need to be on the same subnet. RO 3.71 machines come up on `1.x.x.x` and RO 5.30 machines on `10.x.x.x`, so one had to be moved with `ifconfig`.

**Playing against other platforms:** GCC ignores the `ms_struct` (MSVC bitfield layout) attribute on non-x86 targets, so `SpecialClass` and the lobby packet's bitfields were smaller on RISC OS than on other builds. For example, `EventClass` was 19 bytes instead of 22. Fillers in `special.h` and `defines.h` make every network structure the same size and layout as the macOS (clang) build. This was checked by compiling a size and offset probe with both compilers. For a Mac opponent:

```sh
cmake -S . -B build/mac-net -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/sdl12-compat \
    -DBUILD_VANILLARA=OFF -DSDL1=ON -DSDL2=OFF -DOPENAL=OFF -DNETWORKING=ON
cmake --build build/mac-net
riscos/scripts/run-mac.sh build/mac-net
```

Both players must be built from the same source.

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
!VanillaTD.!Run            !VanillaTD.gamedata.MIX.CONQUER (CONQUER.MIX)
!VanillaTD.!RunImage       !VanillaTD.MIX.TEMPICNH    (TEMPICNH.MIX)
!VanillaTD.INI.CONQUER     (CONQUER.INI)
!VanillaTD.000.SAVEGAME    (SAVEGAME.000, created by the game)
```

- **Reading:** the game tries the short form first, then the plain name, so data copied with long names still works on HostFS or a long-filename disc.
- **Writing:** the extension directory is created when needed. See `common/riscos_fs.h`.
- **Directory scans:** `Find_First` searches both layouts.
- **Deploying:** `deploy-td.sh` lays the data out this way and warns about any name over 10 characters.
- **Fewer filing system calls:** opening a file through UnixLib takes a few milliseconds on a Risc PC, and the game opened 1631 files on the way to GDI 1. Most were probes for loose files that would override the MIX files, and the rest reopened the MIX files for every file read from them. So each directory is listed once and a file it doesn't contain fails without a filing system call (`VC_NODIRCACHE` turns this off). Files opened for reading are also kept open, up to 8, and reused when opened again. Anything opened for writing or deleted closes them all and clears the listings. Start-up to the first game frame went from 12.4 s to 6.0 s on the StrongARM Risc PC. `VC_FSTEST=1` checks this layer (writes, other spellings of a name, deletes) in the current directory, then exits.
- **Testing on the Mac:** build with `-DSHORT_FILENAMES=ON`, and set `VC_FSLOG=1` to trace file opens.

This was verified by copying the app (without `MIX.MOVIES`, since the 256 MB HardDisc4 can't hold it) to `ADFS::HardDisc4.$` on RISC OS 3.71. From there, GDI 1, Save and Load all work.

Without `MIX.MOVIES` the game just skips the movies, so it can be left out on small discs. SCORES (37 MB) holds the music, which isn't used until sound is implemented.

## Making a release

```sh
git tag v<version>-riscos                   # the title screen shows tags starting with v
riscos/scripts/make-release.sh <version>    # -> build/release/VanillaTD-riscos-<version>.zip
```

`make-release.sh` builds the game, then assembles `!VanillaTD` in `build/release`, with:
- `!RunImage` and `Utils.vcprep`
- `!Help`: the user documentation (requirements, installing the data, settings, network play, known issues)
- `!Sprites`/`!Sprites22`: made from `icons/vanillatd-icon.png` by `tools/make-sprites.py`; `tools/make-icon.py` makes that picture (and RA's) from the OpenTS banner
- `Modules`: SharedUnixLibrary and DigitalRenderer
- `Licences`: GPL-3 with EA's additional terms, blast, UnixLib and DRenderer

It leaves out the game data. `tools/riscos-zip.py` stores each `,xxx` filetype in the zip's Acorn extra field, which SparkFS, SparkPlug and Info-ZIP read.

**Layout:** the zip holds only `!VanillaTD` (or `!VanillaRA`); the CD images are dragged onto its icon.

**Tested on RO 3.71:** Prepare double-clicked in a directory holding the GDI ISO, the Nod ISO under a truncated name (`CNC95_Nod_`) and a DOS ISO. It installed both discs including the movies, skipped the DOS disc, and the intro then played. Before the Filer had seen `!VanillaTD`, it gave its error instead. From the command line, `-nomovies` and a bare image name both work.

**Tested:** SparkFS on RISC OS 5.30 unpacked the release zip with every filetype intact. On RISC OS 3.71 the app was copied to ADFS, `Prepare` installed the GDI ISO (`-nomovies`), and the game started. On first run the game plays the intro and goes straight to side selection, because the CD's `CONQUER.INI` has `PlayIntro=Yes`. That's the original game's behaviour, and it then sets it to `no`.

## Day-to-day loop

```sh
VC_DEBUG=1 riscos/scripts/deploy-td.sh        # cross-build (with debug log) and deploy
riscos/scripts/run-td.sh 371 -AUTOSTART=G1    # reset machine, launch TD
riscos/scripts/screenshot.sh 5920 /tmp/s.png  # look at it
tail ../vcport-work/hostfs/'!VanillaTD'/stderr # debug log (flushed in 1 KB blocks)
```

When debugging, `rpcemu-debug trace config data_abort=1 prefetch_abort=1 undefined=1` stops the emulator at the faulting instruction. `scripts/guest-stack.py` gives a heuristic backtrace. On the Mac, `scripts/run-mac.sh build/mac-ubsan -AUTOSTART=G1` runs the sanitizer build and writes reports to `../vcport-work/ubsan.*`.

Game data, the HostFS deploy directory and the Mac run directory all live in `../vcport-work`, next to the repo (`VC_WORK` overrides it). They're kept outside the repo, since the game data isn't redistributable and the builds and logs run to gigabytes. Keep both out of cloud-synced folders: Synology sync once reverted local edits here.
