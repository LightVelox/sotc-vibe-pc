# Progress

Last updated: 2026-09-24. Target: SCES-53326 v1.00 (see `GAME_BUILD.md`).

## Working

* **Build identification**: disc, boot ELF, modules and IRX files fingerprinted (SHA-256); Redump
  match confirmed via PCSX2's bundled database. Tooling and the executable refuse other builds.
* **XFF module format** reverse-engineered (`Docs/XFF_FORMAT.md`); static relocation of KERNEL,
  MANAGER and GAMECORE is byte-identical to PCSX2 except at import sites re-patched at run time.
* **Static recompilation** of the boot ELF + all three modules (9,201 functions, 0 decode failures,
  0 unhandled instructions). 26,981 run-time relocation sites read their immediates from guest RAM;
  constant propagation never folds values produced by those sites (`Tools/verify_reloc_sites.py`).
* **Native boot through the original loader**: the recompiled boot ELF loads, relocates and enters
  KERNEL, MANAGER and GAMECORE at exactly PCSX2's addresses; `module_guard` verifies each module's
  code in guest RAM against the recompiled image. `checkpoint_boot`: every KERNEL section equals
  PCSX2 at KERNEL entry.
* **IOP**: the game's IOP modules (SIO2MAN, DBCMAN, SIO2D, DS1O_D, LIBSD, `sg2iop_driver`, MC2_D) load
  from the disc image and EE buffers and execute on the runtime's IOP emulator; the sound driver's RPC
  server comes up.
* **Disc**: ISO-backed `cdrom0:` (VFS, IOP loader, `sceCdSearchFile` with real LBNs, raw sector reads);
  `NICO.DAT` boot data and root segment load; the sheet system links its data and re-patches code.
* **Timing model for HLE'd I/O**: disc opens/stat/search/reads block the calling guest thread for a
  modelled DVD latency (seek + transfer). This reproduces the PS2's thread interleaving, which the game
  depends on (heap layout, module addresses, data-module linking before sound init).
* **VSync** follows the GS video mode set by the game (PAL 50 Hz).
* **Pad input**: the game's own SIO2MAN, DBCMAN and DS1O_D run unmodified on the IOP emulator against an
  emulated SIO2 controller (`0x1F808200`, IRQ 17, DMA channels 11/12). Port 0 holds a `VirtualDualShock2`
  (digital/analog/pressure modes, config commands 0x40–0x4F, vibration map) fed from the host keyboard or
  gamepad. DS1O_D switches it to analog (0x73); DBCMAN delivers pad data to the EE work area
  (`0x14DA00`) through `sceSifSetDmaIntr`, and libpad2's `scePad2Read` sees it. Verified: D-pad down moves
  the language-menu cursor and Cross leaves the menu. Keyboard: arrows = D-pad, WASD / IJKL = left / right
  stick, X or Space = Cross, C or Backspace = Circle, Z = Square, V = Triangle, Q/E = L1/R1,
  Left/Right Shift = L2/R2, Enter = Start, Tab = Select, F/G = L3/R3; gamepad 0/1 map to ports 0/1.
* **Language menu renders correctly** (content matches a PCSX2 GS dump draw for draw): green
  background, five labels with drop shadow, cursor. Fixed on the way: VIF1 `DIRECT` IMAGE continuation
  (the font/CLUT uploads were corrupted and swallowed the following packets) and exact depth for
  flat-Z triangles (float barycentrics dropped pixels under `ZTST=GEQUAL`). Presentation is still wrong
  (see blockers).
* **EE FPU / VU0 macro semantics**: saturation instead of Inf/NaN, EE divide by zero, `sqrt(|x|)`,
  `VRSQRT = fs/sqrt(|ft|)`, `CVT.W.S` saturation; MXCSR round-toward-zero + FTZ/DAZ on the game thread.
* **Diagnostics**: game TTY (`[GAME]` = guest stdout), categorized logs (`SOTC_TRACE`), stack watchdog
  with VSync rate and EE thread/semaphore snapshot (`SOTC_WATCHDOG`, `SOTC_WATCHDOG_THREADS`),
  sampling profiler (`SOTC_PROFILE=delay:seconds`), guest call tracer (`SOTC_TRACE_CALLS`), thread
  tracer (`SOTC_TRACE_THREADS`), scripted pad input (`PS2X_PAD_SCRIPT=seconds:button[+button][:hold],...`),
  RAM dumps at a function entry with an optional memory condition
  (`SOTC_DUMP_RAM_AT=addr:file[:when=addr=value]`), missing guest functions stop the run.
* **GS diagnostics**: `PS2X_GS_TRACE=firstVsync:count:file` writes every draw (`D`, full context
  state + vertices), transfer (`T`), image payload (`I`, hex) and field (`V`, PMODE/DISPFB/DISPLAY) and
  dumps raw VRAM at the start of the window to `file.vram`; `PS2X_GS_TRACE_DUMP_DRAW=n` also dumps VRAM
  after the n-th traced draw to `file.vram.draw`. `Tools/gstrace_summary.py <trace> [vsync]` collapses
  draws; `Tools/gs_vram.py` decodes CT32/PSMT4 regions of a VRAM dump to PNG.
* **PCSX2 oracle**: `Tools/pine.py trap` parks PCSX2's EE at an exact address (after the ELF/modules are
  in memory) and dumps RAM; `Tools/compare_ram.py` diffs dumps per module section;
  `Tools/win/capture_window.ps1` captures native or PCSX2 frames. GS dumps: start
  `Tools/pcsx2-oracle/pcsx2-qt.exe -batch -fastboot -- <iso>`, focus it and press Shift+F8 (single-frame
  dump into `Tools/pcsx2-oracle/snaps/`, uncompressed); `Tools/gsdump.py <dump> --out trace.txt --vram
  vram.bin --screenshot shot.png` converts it to the same trace format (VRAM is at offset 0x1A9 of the
  version-9 state). The language menu is up ~25 s after launch in PCSX2, ~60 s natively.

## Partially working

* Rendering: the software GS rasterizer draws the menu correctly but slowly (~5 fields/s on the menu,
  ~80% of game-thread time in `GSCpuBackend`, mostly full-screen textured sprites of the bloom chain).
  Boot is ~2× faster than before (MANAGER at 3.5–4 s, GAMECORE at 6–7 s) after batching IOP execution
  and making scheduler snapshots lazy.
* Presentation: the window shows the 512-pixel-wide framebuffer unscaled inside a 640×448 view (black
  side bars), instead of the DISPLAY rectangle (DISPFB1 FBW=8 PSMCT24 at 0, DISPLAY 2560/5 × 512,
  SMODE2=1 interlaced frame mode) scaled to 4:3. During loading the spinner (bottom right) blinks
  every other frame and jumps upwards for a while; in PCSX2 it stays put. Likely causes: the game
  switches `SetGsCrt` from NTSC (mode 2) to PAL (mode 3) mid-boot and the presenter ignores
  DISPLAY/DISPFB/PMODE, plus `GS::updatePreferredDisplaySourceForDraw` (an upstream heuristic that picks
  a "preferred" source buffer from a 640×448 copy pattern) and the double-buffer choice. Not yet
  confirmed with a trace.

## Known blockers

| Area | Blocker | Plan |
|---|---|---|
| Rendering speed | Per-pixel scalar software GS (`GSCpuBackend`) dominates frame time | Hardware GS backend behind a renderer interface, or a SIMD/tiled software rasterizer; profile-driven |
| Presentation | `DISPLAY`/`DISPFB`/`PMODE`/`SMODE2` not applied; spinner blinks/moves during loading | present what the CRTC would scan out (both circuits, DBX/DBY, magnification, interlace) scaled to 4:3; compare with PCSX2 screenshots during loading |
| Audio | `sg2iop_driver` drives SPU2 through LIBSD imports; runtime has no IOP-side SPU2 | SPU2 register model on the IOP side feeding a host mixer |
| Memory card | MC2_D now completes its SIO2 transfers, but ports 2/3 answer "no device" | memory-card device on SIO2 ports 2/3 (next to `VirtualDualShock2`), backed by a host file; compare the post-language-menu screens with PCSX2 |
| Rumble | Motor values reach `IopHost::padVibration`, but raylib's GLFW backend cannot drive rumble | host rumble backend (XInput/SDL) |
| FMV | FFmpeg disabled | decide decoder strategy |
| Remaining EE overhead | `advanceEeTimers` per dispatch (~5%), 8 EE cycles counted per recompiled-function dispatch | batch timer updates; revisit cycle accounting once rendering is fast |

## Temporary hacks / modelled behaviour

| What | Where | Why | Exit criterion |
|---|---|---|---|
| FFmpeg disabled (MPEG → stub frames) | root `CMakeLists.txt` | avoid third-party prebuilt downloads during bring-up | FMV strategy decided |
| HLE bindings for SIF/file/CD/DECI2/TTY/MPEG/IPU | `Port/recomp/sotc.toml`, `Port/src/sotc/hle/` | runtime IOP bridge is API-level | revisit per subsystem against PCSX2 |
| Disc latency model with field (20 ms) granularity | `Port/src/sotc/hle/sce_fileio.cpp`, `sce_cdvd.cpp` | real reads block the caller; the game's thread interleaving depends on it | sub-field timed waits in the EE scheduler; calibrate against PCSX2 |
| SCE fio calls without a VFS equivalent return SCE error codes | `Port/src/sotc/hle/sce_fileio.cpp` | no IOP FILEIO server; each call is logged | implement if the game uses them |
| SIO2 transfer latency: 1,000 + 1,200 IOP cycles per byte; SIF DMA completion 64 cycles + 1 per 4 bytes | `ps2xIOP/src/emulator/devices/iop_sio2.cpp`, `iop_emulator.cpp` | order of magnitude of a 250 kHz pad link | calibrate against PCSX2 if pad latency matters |
| IOP VBlank runs at NTSC 59.94 Hz while the game is PAL | `ps2xIOP/src/emulator/iop_emulator_const.h` | pre-existing; DS1O_D polls the pad per IOP VBlank | make the IOP VBlank follow the GS video mode like the EE side |
| IOP runs in batches of 128 IOP cycles (1,024 EE cycles) | `ps2xIOP/src/emulator/iop_emulator.cpp` (`kIopBatchCycles`) | stepping the IOP one cycle per dispatch cost ~25% of loading time | — (well below any observable latency) |
| Module load addresses in the profile are measured | `Port/profile/SCES-53326_v1.00.json` | only used offline for recompilation; verified at run time | — |

No game function is stubbed with placeholder return values.

## Known deviations from PCSX2 at `checkpoint_boot`

* `argc/argv` not passed to the boot ELF (PCSX2: `argc=1, argv[0]="cdrom0:\SCES_533.26;1"`).
* Kernel object IDs start at 1 (PCSX2's BIOS starts semaphores at 0).
* EE-side state of HLE'd libraries (`_sceCd_*`) is not initialised.

## Reverse-engineering discoveries

* The boot ELF is a dynamic loader for `xff2` modules; KERNEL.XFF exports the boot ELF's symbols.
* Game code keeps global symbol names; static functions are unnamed. See `Docs/ADDRESS_MAP.csv`.
* Unresolved imports are bound to KERNEL `0x001B28F8`; the sheet system (`sheetMgrResolve` →
  `isysResolveAllProgramModule`) later re-patches MANAGER/GAMECORE code when `NICO.DAT` data defines
  them (e.g. `SH_SpuLayoutDEFAULT = 0x0154AA30`).
* The loader's heap and thread timing determine module addresses: KERNEL's graphics-init thread (prio
  45) must allocate its packet buffers (2×0x7C800 + 2×0x630 + later 0x24/0x320/0x200/0x14) while the
  loader thread (prio 45) is blocked in `sceCdSearchFile`, or MANAGER lands elsewhere.
* MANAGER init: `sheetMgrInit` starts the sheet thread (prio 43) and waits on the flag `0x012939A0`;
  the sheet thread runs `sheetfileInit → sheetFileLoadBootData → sheetMgrResolve →
  sheetFileLoadRootSegment`, then sets the flag; `initSoundManager` follows.
* `adpcmFileInit` allocates 0x58000 bytes of IOP memory; the IOP needs a realistic free heap.
* KERNEL reads `NICO.DAT` through a 32 KB sector cache at `0x001EFA40` (`sub_001A7560`), with a host-file
  mode and disc-error recovery.
* The loader's fatal path (`iosJumpRecoverPoint` → `LoaderSysJumpRecoverPoint`) drops the calling
  thread to priority 127 and draws the error screen; messages go to the TTY.
* libkernel `_InitSys` locates the kernel syscall table with two temporary syscalls (see
  `Analysis/FUNCTION_NOTES.md`).
* `SetGsCrt` video modes: 2 = NTSC, 3 = PAL. The game boots in NTSC and switches to PAL during loading;
  the menu uses SMODE2=1 (interlaced, frame mode) with a 512×512 PSMCT24 display buffer at FBP 0.
* Menu frame structure: the scene renders into FBP 0xE8 (512×416 CT32), a bloom chain runs through FBP
  0x150/0x1B8/0x1D8/0x1E0/0x1FE, and the result is stretch-copied (416→512 lines) into the display
  buffer at FBP 0 at the start of the next frame. FBP 0x150 aliases the font texture (TBP 0x2A00, PSMT4
  128×128, CLUT CBP 0x3F3B CSM1), so the game re-uploads the font every frame through VIF1 `DIRECT`
  (`DIRECT 1` = IMAGE tag, then `DIRECT n` in the REF tag's TTE word = pixels).
* The game's TEX1.K for the menu text differs from PCSX2 (0xFEC vs 0xFCE); harmless here because both
  select magnification, but it points at an EE float difference worth checking later.
* IOP import map: `sg2iop_driver` → libsd, sifcmd, sysclib, thbase; `DS1O_D` → sio2man, sio2d, dbcman;
  `MC2_D` → sio2man, sio2d, dbcman, secrman, cdvdman.

## Next priorities

1. Presentation: apply DISPLAY/DISPFB/PMODE/SMODE2 (including the NTSC→PAL switch) and fix the loading
   spinner (blinking, jumping) against PCSX2 screenshots; remove/replace the preferred-source heuristic.
2. Rendering performance: the software GS is the limiter (~5 fields/s on the menu). Options: tiled
   multi-threaded rasterizer with fast paths for the bloom sprites, or a GPU backend behind the existing
   `GSRasterBackend` interface.
3. Memory card on SIO2 ports 2/3 (the screens after the language menu depend on it).
4. Audio: SPU2 on the IOP side.
5. Sub-field timed waits in the scheduler; calibrate disc timing against PCSX2; IOP VBlank at the PAL rate.
6. argv, kernel object ID numbering.
