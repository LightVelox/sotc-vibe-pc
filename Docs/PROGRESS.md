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
  flat-Z triangles (float barycentrics dropped pixels under `ZTST=GEQUAL`).
* **Presentation follows the PS2 CRTC** (`GSCpuBackend::PresentFromLocalMemory`): both circuits from
  PMODE/DISPFB/DISPLAY (DX/DY/DW/DH, MAGH/MAGV, DBX/DBY), union of the display rectangles, circuit 2 or
  BGCOLOR as the base and circuit 1 blended by `ALP` (MMOD=1) or pixel alpha (MMOD=0); SMODE2 INT+FFMD=0
  weaves the full frame, INT+FFMD=1 halves the buffer height. No substitution heuristics: the upstream
  preferred-source and "black display -> show a context frame" fallbacks are gone from presentation. The host
  window (640x480 default) shows the frame at 4:3 with bilinear filtering and resizes its texture to the
  frame. The menu/loading setup (PMODE 0x8023, both circuits on the same 512x512 CT24 buffer, circuit 2 one
  line lower, ALP 0x80) is the game's de-flicker filter and is reproduced (512x513 output). Verified with
  window captures: the loading spinner stays in place for the whole loading screen and the menu framing
  matches PCSX2. The NTSC->PAL switch is followed because the registers are read every field.
* **Boot flow past the menus**: language menu -> 50/60 Hz choice -> loading -> "No memory card" screen
  (Retry/Continue) -> Continue -> "Sony Computer Entertainment Europe presents" over the real-time 3D opening
  scene (Agro, cliffs, fog, bloom), matching a PCSX2 capture closely. Two fixes were needed:
  * **GS interrupt (INTC 0)**: the runtime never raised it. The game's GP thread unmasks FINISH (IMR 0xFD00)
    and counts GS FINISH interrupts (`sub_001BE618` -> GP-finish callback `sub_01356E08` -> counter
    `0x014770E8`); `st_reloadtask` waits on that counter (`st_WaitGPFinish`) and hung forever after the
    50 Hz choice. `EeScheduler` now raises cause 0 whenever a CSR event (SIGNAL/FINISH/HSINT/VSINT/EDWINT)
    becomes pending with its IMR bit clear; CSR bits 0-4 are write-1-to-clear and VSINT is set at VBlank.
  * **`sceCdReadIOPm`** was an alias of `sceCdRead` and wrote sound data (PS-ADPCM) meant for IOP address
    0x100000 over the EE boot image at 0x00100000 (code and `.rodata`), which later crashed `vfprintf`
    through its corrupted jump table. It now reads into IOP RAM and uses the same drive timing model.
* **Opening cutscene and 3D cameras**: after the SCEE logo the real-time cloud/hawk cutscene renders
  (letterboxed, as in PCSX2) instead of a flat slate-grey screen. Root cause: every `R5900Context` except
  the boot thread's started with VU0 `VF00 = (0,0,0,0)` (`EeScheduler::startThread` and interrupt/alarm/
  callback invocations default-construct their contexts). `iosGetSinCosf` computes cos as
  `sqrt(VF00.w - sin^2)`, so on game threads cos = |sin|, `iosGetTanf` returned 1.0 for every angle, the
  eye-to-screen distance was 208 instead of 502.2 and the camera matrices degenerated (the `FLT_MAX` view
  matrix on the menu). `VF00` is now part of the `R5900Context` reset state. Side effects: the menu no
  longer draws the solar-flare pass (the frustum test fails, as in PCSX2) and runs at ~7.4 fields/s
  instead of 4.5 (1.7 on the 50/60 Hz menu); boot reaches the SCEE logo at ~112 s instead of ~165 s.
* **EE FPU / VU0 macro semantics**: saturation instead of Inf/NaN, EE divide by zero, `sqrt(|x|)`,
  `VRSQRT = fs/sqrt(|ft|)`, `CVT.W.S` saturation; MXCSR round-toward-zero + FTZ/DAZ on the game thread.
* **Diagnostics**: game TTY (`[GAME]` = guest stdout), categorized logs (`SOTC_TRACE`), stack watchdog
  with VSync rate and EE thread/semaphore snapshot (`SOTC_WATCHDOG`, `SOTC_WATCHDOG_THREADS`),
  sampling profiler (`SOTC_PROFILE=delay:seconds`), guest call tracer (`SOTC_TRACE_CALLS`), thread
  tracer (`SOTC_TRACE_THREADS`), scripted pad input (`PS2X_PAD_SCRIPT=start:button[+button][:hold],...`; `start` in seconds, or in
  VSync fields with a `v` suffix, e.g. `290v:cross,375v:cross,770v:down,800v:cross` reaches the SCEE logo
  without a memory card; field-timed steps hold 10 fields by default),
  RAM dumps at a function entry with an optional memory condition
  (`SOTC_DUMP_RAM_AT=addr:file[:when=addr=value]`), missing guest functions stop the run.
* **GS diagnostics**: `PS2X_GS_TRACE=firstVsync:count:file` writes every draw (`D`, full context
  state + vertices), transfer (`T`), image payload (`I`, hex) and field (`V`, PMODE/DISPFB/DISPLAY) and
  dumps raw VRAM at the start of the window to `file.vram`; `SOTC_DUMP_RAM_AT` also takes `:after=seconds`
  and `SOTC_TRACE_CALLS` prints `$f12`-`$f15`; `PS2X_GS_TRACE_DUMP_DRAW=n` also dumps VRAM
  after the n-th traced draw to `file.vram.draw`. `Tools/gstrace_summary.py <trace> [vsync]` collapses
  draws; `Tools/gs_vram.py` decodes CT32/PSMT4 regions of a VRAM dump to PNG.
* **PCSX2 oracle**: `Tools/pine.py trap` parks PCSX2's EE at an exact address (after the ELF/modules are
  in memory) and dumps RAM; `Tools/compare_ram.py` diffs dumps per module section;
  `Tools/win/capture_window.ps1` captures native or PCSX2 frames. GS dumps: start
  `Tools/pcsx2-oracle/pcsx2-qt.exe -batch -fastboot -- <iso>`, focus it and press Shift+F8 (single-frame
  dump into `Tools/pcsx2-oracle/snaps/`, uncompressed; Ctrl+Shift+F8 has to be *held* for a multi-frame
  dump, e.g. with `keybd_event` key-down/key-up); `Tools/gsdump.py <dump> --out trace.txt --vram
  vram.bin --screenshot shot.png` converts it to the same trace format (VRAM is at offset 0x1A9 of the
  version-9 state; one `V` line per VSync). In PCSX2 (`-fastboot`) the loading spinner is up at ~4-8 s
  and the language menu at ~8.5 s; natively the spinner shows at ~2-18 s and the menu at ~19 s.

## Partially working

* Rendering: the software GS rasterizer draws the menu correctly but slowly. Measured 2026-09-24:
  ~18 fields/s while loading, ~4.5 fields/s on the menu (before the VF00 fix; ~7.4 after it), ~4.6 on the
  SCEE logo scene and ~0.9 on the cloud cutscene. Menu profile (`SOTC_PROFILE`): 80% of game-thread
  time under `GS::processGIFPacket` -> `GSCpuBackend` (sprites 47% incl., triangles 32% incl., texture
  sampling 32%, `WritePixel` 22%); `advanceEeTimers` ~6%, `iosRecvMsg` ~7%. A native menu frame is ~10.5 M
  pixel operations, most of them from the solar-flare pass that the VF00 fix removed.
  Boot is ~2× faster than before (MANAGER at 3.5–4 s, GAMECORE at 6–7 s) after batching IOP execution
  and making scheduler snapshots lazy.
* Loading spinner blinking: at the end of the loading screen the game's frame loop drifts against VSync
  (odd fields end with "copy scene -> display, clear scene", even fields start by copying the just-cleared
  scene), so every other field shows a black display buffer. A multi-frame PCSX2 GS dump shows the same
  sequence (V39-V73), so this is game behaviour: on the PS2 it lasts ~0.7 s at 50 fields/s and reads as a
  dimmed spinner; natively it lasts ~2.6 s at a few fields/s and is visible as blinking. It shrinks with
  rendering speed. The former "spinner jumps up" was the removed black-display fallback showing the 416-line
  scene buffer unscaled.

## Known blockers

| Area | Blocker | Plan |
|---|---|---|
| Rendering speed | Per-pixel scalar software GS (`GSCpuBackend`) is 80% of game-thread time on the menu and runs synchronously on the game thread | recommended: GS on its own thread, then a rewritten software rasterizer (per-draw setup, edge-function/span triangles, sprite fast paths, direct swizzle tables), then band-parallel rasterization; GPU backend later behind `GSRasterBackend` |
| Audio | `sg2iop_driver` drives SPU2 through LIBSD imports; runtime has no IOP-side SPU2 | SPU2 register model on the IOP side feeding a host mixer |
| Memory card | MC2_D now completes its SIO2 transfers, but ports 2/3 answer "no device"; the game shows "No memory card inserted" (Continue works) | memory-card device on SIO2 ports 2/3 (next to `VirtualDualShock2`), backed by a host file; compare the post-language-menu screens with PCSX2 |
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
* Solar flare: `solarFlare` (`0x01196EA0`) switches on `0x0128FE70` (1 -> `sub_01193A78`, 2 ->
  `sub_011952E0`; PCSX2 and native both have 2, colour d0c498). `sub_011952E0` projects the sun direction
  (`0x01296320`, z = sqrt(1-x^2-y^2), x40000) through `get_cur_camera_context()+0x5A0` and draws only if
  |x/w|, |y/w| < 1.07 and z/w > 0. Camera contexts: index at `0x012939B4`, table at `0x01334200`.
* Boot game flow (`st_gameflow` `0x01361858`): `languageMenuLoad` -> `bootChooseLangScript` -> wait
  `menuSelected` -> `bootSelectDisplayMode` -> `commonDataLoad` -> loop until `gcCheckStageLoadFinish`
  (`[0x014778AC] == -1`, set at the end of `st_reloadtask` `0x01367398`). Upstream `sceCdRead` still has an
  "alternative argument order" fallback that can write to arbitrary addresses on unresolved reads; worth
  removing when it bites.
* IOP import map: `sg2iop_driver` → libsd, sifcmd, sysclib, thbase; `DS1O_D` → sio2man, sio2d, dbcman;
  `MC2_D` → sio2man, sio2d, dbcman, secrman, cdvdman.

## Next priorities

1. Rendering performance (~7.4 fields/s on the menu, ~0.9 on the cloud cutscene; disc throughput is capped
   by the field rate because the drive model completes reads on field boundaries): GS on its own thread,
   rewritten software rasterizer, then band-parallel rasterization (see blockers).
2. Compare the opening cutscene with a PCSX2 GS dump draw for draw (clouds, hawk, cliffs); re-check the
   menu against PCSX2 now that the solar flare is gone (the faint stripes and the TEX1.K difference may
   have had the same cause).
3. VU0 register file is per context (each thread/handler has its own copy); on the PS2 it is shared.
   Harmless so far, revisit if a game thread relies on another thread's VU0 state.
4. Memory card on SIO2 ports 2/3.
5. Audio: SPU2 on the IOP side.
6. Sub-field timed waits in the scheduler; calibrate disc timing against PCSX2; IOP VBlank at the PAL rate.
7. argv, kernel object ID numbering.
