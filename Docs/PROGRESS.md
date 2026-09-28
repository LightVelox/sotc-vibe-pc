# Progress

Last updated: 2026-09-27 (seventeenth session: memory-card format, save and load). Target: SCES-53326 v1.00
(see `GAME_BUILD.md`).

## Working

* **Build identification**: disc, boot ELF, modules and IRX files fingerprinted (SHA-256); Redump
  match confirmed via PCSX2's bundled database. Tooling and the executable refuse other builds.
* **XFF module format** reverse-engineered (`Docs/XFF_FORMAT.md`); static relocation of KERNEL,
  MANAGER and GAMECORE is byte-identical to PCSX2 except at import sites re-patched at run time.
* **Static recompilation** of the boot ELF + all three modules (9,216 functions, 0 decode failures,
  0 unhandled instructions). 26,981 run-time relocation sites read their immediates from guest RAM;
  constant propagation never folds values produced by those sites (`Tools/verify_reloc_sites.py`).
* **Native boot through the original loader**: the recompiled boot ELF loads, relocates and enters
  KERNEL, MANAGER and GAMECORE at exactly PCSX2's addresses; `module_guard` verifies each module's
  code in guest RAM against the recompiled image. `checkpoint_boot`: every KERNEL section equals
  PCSX2 at KERNEL entry.
* **IOP**: the game's IOP modules (SIO2MAN, DBCMAN, SIO2D, DS1O_D, LIBSD, `sg2iop_driver`, MC2_D) load
  from the disc image and EE buffers and execute on the runtime's IOP emulator; the sound driver's RPC
  server comes up. SPU2 has a voice model on the IOP side (see "Tutorial hints"), no audio output yet.
* **Memory card (seventeenth session)**: enabled by default (`PS2X_MEMCARD=0` in `sotc.ini` disables it; see
  `Docs/CONFIGURATION.md`); the game's card driver now completes its
  DBCMAN/MC2_D requests, reads a blank card, offers to format it, formats it, and writes a shrine save.
  A fresh game launch lists the saved "Shrine of Worship" entry under Load Game and restores Wander to
  gameplay at the shrine. The raw `memcards/Mcd001.ps2` image remains 8,650,752 bytes (16,384 pages of
  528 bytes); PCSX2 identified a copy of the formatted, saved image as "8 MB, Formatted", listed its
  shrine save under Load Game, and loaded it into gameplay at the shrine. Loading a PCSX2-created save
  in the port remains to be checked.
  The five no-card GS goldens compare SAME with `PS2X_MEMCARD=0`,
  `ps2x_tests` passes 477/477, IOP CTest passes 6/6, and the save-state round trip compares SAME.
  The scripted shrine-save test uses the opt-in `SOTC_POKE` diagnostic to request gameflow state 5.
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
  * Stick center is 0x7F (fifteenth session), as in PCSX2 (read from the game's pad buffer `0x12914B4` over PINE:
    `7f7f7f7f` at rest). The game reads raw stick bytes somewhere (not only through the 0.4 dead zone of
    `iosPadGetXZInputL/R`): with 0x80 at rest a scripted run diverged from PCSX2 before the first ride (Agro arrived at a
    different spot) and the user saw Agro drift right; with 0x7F the same script matches PCSX2 at fields 2550-2600.
    Gamepad axes map -1..1 to 0x00..0xFF around 0x7F.
  * Right stick inverted on both axes by default in this port (user request; `PS2X_INVERT_RIGHT_STICK=0|x|y|xy`
    overrides, `ps2_host_input::setRightStickInvertDefault`). Only gamepad and keyboard input is flipped; pad scripts
    are not, so scripted runs stay comparable with PCSX2. The game's own options (`0x14770BC`/`0x14770C0` free-camera
    reverse LR/UD, `0x14770B0`/`0x14770B4` aiming) are 0 without a memory card.
* **Save states (sixteenth session)**: F5 quick-saves and F9 quick-loads (`<exe dir>/states/quick.state`,
  ~46 MB, uncompressed); a short overlay confirms. The state is taken at the top of the EE scheduler loop,
  where every guest thread is parked at a resume point: requests wait (a few dispatches) until no interrupt/
  alarm/callback handler is running and no thread holds a host completion closure, then the VU/GIF threads are
  drained. Contents: EE RAM, scratchpad, IOP RAM mirror, GS privileged registers, VIF/DMA/timer/TLB state,
  VU0/VU1 micro and data memory, both VU interpreters (pipelines, flags, cycle), the VU1 timing model, GS
  frontend registers + VRAM (backend `SnapshotVram`/`ImportState`; the GPU backend re-uploads on load), the
  scheduler (threads, waits, semaphores, event flags, alarms, IRQ handlers, deadlines with host time rebased,
  VBlank period), the IOP emulator (memory, kernel, RPC, CDVD, timers, intrman, SIO2 incl. pad, SPU2, imports,
  module manager), VFS descriptors (reopened by path and seeked), the HLE CD/SIF stub state and the syscall
  layer's SIF RPC client/server and module tables, plus registered extensions (Port: the DVD drive model).
  Waits that used lambdas to set the resume PC now use `EeResume` descriptors (`waitVSyncResume`,
  `waitUntilCycleResume`) so they survive a save. Serialization is one bidirectional `serialize(Ar&)` per
  class (`ps2x/state_io.h`, section tags checked on load). Validation (`Tools/state_check.py`, deterministic
  mode): save at field 1000, load in a fresh process at field 300 or at field 1, or reload in the same process
  at field 1200 — the cutscene window 1360-1365 is identical in all cases (checkpoints and 58,922 per-command
  VRAM hashes). In the default MTVU + GPU configuration a gameplay state (Wander riding to the shrine, field
  3000) loads in a fresh process and renders the same frame 100 fields later; frame differences stay within
  the normal run-to-run noise of that (non-deterministic) mode. Test hooks: `PS2X_STATE_SAVE_AT=field:path`,
  `PS2X_STATE_LOAD_AT=field:path`. Memory-card contents are not part of a state (as in emulators).
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
* **GS pipeline (2026-09-24, fifth session)**: the frontend feeds a `GSThreadedBackend` (own thread
  `GSThread`, enabled by the port executable; `PS2X_GS_THREAD=0` turns it off) wrapping `GSCpuBackend`.
  Every call that observes GS results drains the queue first: FINISH/SIGNAL/LABEL, local->host reads,
  `ReadVram`/`SnapshotVram`/`ClearFramebuffer`, presentation, GS trace dumps and the recorder. Commands
  carry the producer's MXCSR, so the GS thread rasterizes under the EE rounding mode like before.
  `GSCpuBackend` got a new rasterizer (`gs_cpu_raster.cpp`, `gs_swizzle.h`): swizzle through page tables
  split into row/column parts, per-draw decoded pixel state (alpha-test table and CLUT palette cached
  across draws), sprite and triangle paths that evaluate exactly the same float expressions as the old
  per-pixel code (hoisted per row/column), a bilinear lerp on 4 channels at once with an `lround`
  replacement proven identical to the CRT for every float |v| <= 1024 in both rounding modes
  (`gs_replay --check-lround`), and band-parallel rasterization over row bands (`GSBandPool`,
  `PS2X_GS_BANDS=n`, default hardware threads - 4, max 8). The texture page cache keeps the old
  stale-until-TEXFLUSH semantics but copies a page only when something writes it (copy-on-write). Draws
  whose sampled texture pages overlap their written pages, invalid formats and the pixel-independent
  CT32 fill fall back to the old per-pixel code (`PS2X_GS_FAST=0` forces it for everything); P4 targets,
  overlapping frame/Z buffers and draws that start on a stale cache page run on one thread. Output is bit-identical to the previous
  rasterizer: five recorded scenes (language menu, 50/60 Hz menu, loading, SCEE logo, cloud cutscene;
  52k draws) replay with a VRAM hash compared after every command, with 1..12 band threads, and the game
  itself produces the same VRAM checkpoints as the recordings.
* **Boot-to-cutscene timing (2026-09-24, sixth session)**: boot -> language menu -> 50 Hz -> "No memory
  card" -> SCEE logo -> first cloud-cutscene frame now takes ~33 s of wall time natively (was ~78 s)
  against ~29.5 s in PCSX2 (`-batch -fastboot`, which spends ~2.5 s in its own startup before the game's
  VBlank counter starts). Every change below is bit-exact: the five golden recordings match command for
  command (per-command VRAM hashes, 54k in the cutscene window) in a full game run
  (`Tools/golden_run.py`), and `ps2x_tests` passes (463 tests).
  * **No C++ exceptions for blocking syscalls**: generated code now emits
    `if (!runtime->handleSyscall(...)) return;`. Inside `handleSyscall` the scheduler defers transfers
    (`EeScheduler::DeferredTransferScope`): `blockCurrent` / `transferIfRequested` set a flag instead of
    throwing and the native stack unwinds through the normal return path (the same path checkpoints use).
    No guest instruction and no cycle accounting happens between the block and the dispatcher, so
    scheduling is identical. Waits entered outside `handleSyscall` (HLE hooks, `waitVSync`,
    `invokeCurrent`, thread exit) still throw. Tests cover sleep/wakeup and semaphore hand-over through
    `handleSyscall`.
  * **Lazy EE timers**: `advanceEeTimers` only accumulates cycles and runs the timer arithmetic when the
    exact cycle of the next compare/overflow interrupt is reached or a timer register is accessed (the
    tick/remainder arithmetic composes exactly; a test compares against a read after every step).
    GIF_STAT.FQC is cleared only if something wrote GIF_STAT since the last advance.
  * **`processPendingEvents` fast path**: deadlines are examined only when one is cycle-due and the event
    queue only when the pending flag is set; before, every return to the dispatcher took three locks,
    read `QueryPerformanceCounter` and allocated a `std::deque`.
  * **GS FINISH/SIGNAL/LABEL no longer drain the GS thread**: `Sync(Finish)` is queued to the GS thread
    (the CPU backend's `Sync` is a no-op and every VRAM observation drains by itself), so the EE builds the
    next frame while the GS thread draws.
  * **Build flags**: `ps2_runtime` gets `/Ob2 /Oi /GL /Gy /Gw` in RelWithDebInfo and `sotc` links with
    `/LTCG`; the generated game code is compiled with `/Ob2` (memory-access helpers were not inlined).
    `/fp:precise` everywhere as before. `/arch:AVX2` for the runtime was measured (no gain) and dropped.
  * Smaller: lock-free presence bitmap before the syscall-override lookup; the per-call `GsGetIMR` /
    `GsPutIMR` logs (stdout flush several times per frame) are gone; the VBlank host deadline chain may
    lag wall time by at most 2 fields (it used to fast-forward to catch up after slow stretches; guest
    timing is cycle-based and unaffected).
  * **Pad script is deterministic**: scripted buttons are evaluated when the game reads the pad, from the
    current field. They used to be sampled by the host render loop, so presses depended on host timing and
    were lost when the window was occluded.
* **Idle thread, IOP scheduling and disc timing (2026-09-24, seventh session)**: every phase before the
  cutscene now runs at the paced 50 fields/s; the first cutscene frame comes at ~27 s of wall time (field
  1215; was ~33 s / field 1329; PCSX2 ~29.5 s). The goldens were re-recorded twice for the timeline changes
  below (the old set is not kept in the repo).
  * **Idle thread skip**: the game's lowest-priority thread (`sub_001A8068`) polls
    `iosRecvMsg(0x1F7A70, 0, 0)` forever. `iosRecvMsg` is replaced by a wrapper (`Port/src/sotc/idle_thread.cpp`)
    that, only for the call from the idle loop (`$ra == 0x1A808C`), calls `EeScheduler::skipIdleCycles()`:
    when no thread at the same or higher priority is ready, no interrupt/invocation is pending and no
    reschedule is requested, EE time advances in 1,024-cycle steps (the IOP runs as before, EE timer
    interrupts are hit exactly) until the next scheduler deadline or until something becomes pending. One
    idle iteration then runs normally. It used to take ~51,600 iterations per field (60-70% of the game
    thread in the menus/logo). `SOTC_IDLE_SKIP=0` turns it off.
  * **IOP threads preempt like the real IOP kernel**: `SignalSema`, `SetEventFlag`, `StartThread`,
    `WakeupThread` and `ResumeThread` (thread-context variants) yield when they make a higher-priority IOP
    thread ready. Before, the woken thread only ran at the next 128-cycle IOP batch boundary, so SIO2MAN's
    request/acknowledge event-flag protocol depended on where batch boundaries fell; the idle skip moved
    them and the pad thread deadlocked (the pad stopped updating at tick 43).
  * **IRX `module_start` runs in a real IOP thread** (`kModuleStartPriority` 8, 16 KB stack) and the IOP
    scheduler runs until it returns (at most 2 s of IOP time). It used to be a plain call outside any
    thread, where `WaitEventFlag`/`WaitSema` fail: DS1O_D's start-up SIO2 transfers left stale bits (0x401) in
    SIO2MAN's event flag and only worked by accident of scheduling. Test: `ps2_iop_emulator_tests` (a
    `module_start` that blocks on a flag set by its own threads, and a low-priority setter that must hand
    over to the higher-priority waiter immediately).
  * **Disc reads complete at their EE cycle**: `sceCdRead`/`sceCdReadIOPm` compute the completion cycle
    (`SOTC_DVD_RATE` bytes/s, `SOTC_DVD_SEEK_MS` for non-sequential reads, default 20 ms) and `sceCdSync(0)`
    blocks until exactly that cycle with the new `EeScheduler::waitUntilCycle` (an `ExternalWake` deadline
    without host pacing; `currentCycle()` is public). It used to round up to the next field (3-4 fields
    per 32 KB NICO.DAT read). "No memory card" now appears at field 662 (was 739; PCSX2 684) and the logo is
    shorter because the cutscene data finishes loading sooner. fio `sceOpen`/`sceGetstat`/`sceRead` and
    `sceCdSearchFile` stay field-based: they only run during boot, where the module load addresses depend
    on the thread interleaving.
* **VU1 interpreter 2x faster, bit-exact (seventh session)**: pipeline commits only look at occupied slots
  (valid-slot bitmasks per pipeline) and return immediately before the next ready cycle (lower bound kept by
  every enqueue); `advanceTo`/`flushPipelines` jump over cycles where nothing commits and PATH1 is idle;
  decoded pairs are used by reference; VI read/write scans use bit scans; XGKICK no longer zeroes its 64 KB
  packet buffer per kick. The cloud cutscene went from ~1.6 to ~3.1 fields/s and all five golden windows
  (57,407 per-command VRAM hashes in the cutscene) are identical. `ps2_vu1.h` is in the generated code's PCH,
  so this needed a full rebuild. What remains is spread over the FMAC path (exact-result and flag
  computation, pipeline enqueue): the cutscene needs ~830k VU1 instruction pairs per field, i.e. ~42M pairs/s
  at 50 fields/s, against ~2-3M pairs/s interpreted.
* **VU1 AOT recompilation (eighth session)**, bit-exact against the interpreter:
  * `PS2X_VU1_CAPTURE=dir` writes every distinct 16 KB VU1 code image seen at MSCAL/MSCNT (`vu1_<fnv1a64>.bin`)
    and the (image, entry PC) pairs (`entries.txt`). `Analysis/oracle/vu1_capture/` now holds 11 images and 397
    entries (boot, the whole opening cutscene to field 13000, title view, New Game intro up to field ~3700). The
    compiled code covers every PC of an image, so only new images matter (entries are just resume points); new
    images run interpreted until they are captured and the code is regenerated.
  * `ps2xTest/tools/vu1_recomp <capture-dir> Port/generated/vu1` (built in `C:/tmp/bt`, output git-ignored)
    emits per-op C++ per image: loop-aware blocks of up to 128 pairs, gotos for in-block branches, delay-slot and
    E-bit copies. Registers are written immediately with exact per-lane ready times; MAC/status/clip flags go
    through lazy FIFO rings and are folded on read; XGKICK progress is caught up lazily. FMAC ops take a fast
    path when every destination lane is safely normal (no clamping, no flag other than sign, no cancellation)
    and fall back to the exact double-precision path otherwise; exact zeros (a zero operand of MUL/MADD/MSUB/OPMSUB
    with the accumulator unchanged, x == -y for ADD, x == y for SUB) also stay on the fast path with their Z/S
    flags (eleventh session; `PS2X_VU1_VERIFY` 7.6M runs to field 2006, 0 mismatches). Straight-line segments get a statically
    scheduled copy (entry checks for operand readiness, cycle snapshots instead of per-pair checks).
    `sotc_vu1_code` is built with `/arch:AVX2`.
  * `PS2X_VU1_VERIFY=1` runs every VU1 program twice (compiled, then the interpreter on a copy) and compares
    registers, pipeline state, VU memory and XGKICK packets (0 mismatches over 4.9M runs up to field 1785);
    `PS2X_VU1_RECOMP=0` disables the compiled code.
  * Static segments (fourteenth session): every VF/VI latency is at most 4 cycles and ACC's is 1, so at a segment
    entry any register written before it is ready by entry + 3; only reads at the first three positions can stall and
    they now stall in place (no fallback to the per-pair path; the budget check adds 3 cycles of slack). Segments
    start at every non-breaker start and run through branch targets (joined up to 24 positions). Ready times and the
    branch-delay VI backup are stored once at segment end; branch reads resolve the backup statically. VF/VI
    registers live in locals for the whole segment (loaded at entry, written back at exit). In segments without
    MAC/status reads, FMAC flag records are merged: all but the last three cycles' FMACs accumulate sticky bits in two
    SIMD registers (`fmacV`) and push one aggregated record; an exact-zero product is always on the fast path (the
    result is the normalized ACC). Generated images are split into ~400 KB parts (`vu1_<hash>_<n>.cpp`) so the ~99
    files build in parallel. Tried and dropped: keeping registers in locals across a whole block (MSVC codegen got
    2.4-4.5x slower).
  * Register states in segments (fifteenth session): each VF register kept a raw and a normalized local for the whole
    segment and MSVC spilled heavily. `vu1_recomp` now tracks per register whether raw == normalized ("clean": every
    FMAC result, MINI/MAX, ITOF, moves of clean registers) and then keeps only the normalized local; loads (LQ/LQI/LQD,
    MFIR, MFP, MR32/MOVE of unclean sources) store their raw bits to `vu.m_state.vf` right away and keep only the
    normalized local ("raw stored"; raw reads reload from memory). The segment write-back stores accordingly. Two
    emitter details matter for this state: the upper op's write is generated before the lower op (emission order), and
    `heavy()` probes `upperEmit`/`lowerEmit` under a `RegViewProbe` that restores the view. `sotc_vu1_bench --verify`
    0 mismatches on the shrine/towers/passage traces; 7-10% less VU1 time. Tried and dropped: skipping the XGKICK
    catch-up for VU stores outside the kick's read window (slower).
  * VU1 trace bench (fourteenth session): `PS2X_VU1_TRACE=<dir>/trace.bin:<firstField>:<fields>` records every VU1
    run (VU state + diffed VU memory, code images next to it); `build/port/bin/sotc_vu1_bench <trace> <dir>
    [--repeat N] [--verify] [--profile] [--only <pc>]` replays it through the compiled code (ms and VU cycles per
    field, per image and entry) or verifies compiled vs interpreter for every run in seconds. Configure with
    `-DSOTC_VU1_ALT_DIR=<dir>` to also build `sotc_vu1_bench_alt` from another generated directory for A/B tests.
  * VU1 timing model (fourteenth session, default on, user decision): VU1 cycles are EE cycles (same clock). A VIF1
    DMA start records its EE cycle; a D1_CHCR read waits for the VU thread, charges the VU1 cycles executed since then
    (`busyUntil = max(busyUntil, kick) + cycles`) and, while the EE is early, consumes EE cycles
    (`EeScheduler::consumeCycles`, stops at scheduler events) and reports STR busy. The kernel's
    `iosDmaSendPath1` polls D1_CHCR/VIF1_STAT/VPU_STAT before every send, so the EE waits like on a PS2 and
    overloaded shots draw every other field (towers ~50%, passage ~80% of frames 0.0392 s) at real-time speed.
    `PS2X_VU1_TIMING=0` disables it, a fraction scales it. The cutscene golden was re-recorded for it (SAME with the
    model off). Fifteenth session: the D1_CHCR read no longer waits for the VU thread (that wait was 13% of the EE
    thread): the worker publishes the VU1 cycle counter after each job and the read charges the cycles completed so far;
    the rest is charged at later reads. `PS2X_VU1_TIMING_SYNC=1` restores the blocking accounting. DMA data is copied
    at enqueue time, so the EE never needed the wait for correctness. Canyon/riders 40 -> 46.5 fields/s.
* **GPU GS renderer (eighth session, default)**: `GSGpuBackend` (OpenGL 4.6 compute on the GS worker thread,
  own WGL context and loader) keeps the 4 MB of GS memory in an SSBO with the swizzle tables and ports the
  reference rasterizer to GLSL. Primitives are binned into 16x16 tiles, one workgroup per tile draws them in
  order; batches flush on render-target changes, texture/CLUT page hazards and transfers. CLUT loads of a batch
  run in one dispatch (the CPU resolves which load last wrote each 16-entry block) and repeated identical
  loads are skipped; uploads go through a persistent-mapped 64 MB ring and presents read back asynchronously.
  Against the CPU rasterizer: max per-channel difference 3 in the cutscene, identical loading screen,
  logo/menu differ by the CPU path's round-toward-zero stripes. `PS2X_GS_GPU=0/1` overrides the default,
  `PS2X_GS_GPU_STATS=1` prints presents/s, GPU time and batches/CLUT loads/transfers per frame;
  `gs_replay --backend gpu|gputhread` and `--compare-backends A B [--dump dir]` replay recordings on it.
  * Fifteenth session (output identical to before on the shrine and New Game intro recordings, `--compare-backends cpu
    gpu` statistics unchanged): shrine replay 0.332 -> 0.217 s per 10 fields, intro 2.01 -> 1.10 s per 70 fields.
    * Copy-on-write texture pages: an upload into pages that queued primitives (or queued CLUT loads) still read no longer
      flushes the batch. The old page contents are copied into a shadow area (512 pages after the 4 MB of GS memory) and
      the queued readers are redirected through per-epoch page maps (`SEpoch` state word, `pageMap` SSBO, remap in the
      texture fetch and in the CLUT load shader). Only uploads over pages the batch renders to still flush. The main scene
      went from ~68 raster dispatches per field to ~2. CLUT slots are now counted per batch (a batch can span more than
      1,024 CLUT loads once uploads stop splitting it).
    * Uploads and page copies are queued and run as one copy dispatch plus one upload dispatch (descriptor list + per
      workgroup map) at the next flush or conflict; CLUT loads run once per batch (1,400 -> 90 CLUT dispatches per 10
      shrine fields). The game streams every palette (8x2 at 0x3fXX) and 4-bit texture (64x64 at 0x2fXX) through the same
      small areas, so a write-after-write on a pending page still starts a new transfer phase (~350 small dispatches per
      10 fields remain; each dependent dispatch costs ~15 us of GPU time regardless of barrier bits).
    * Raster loop: per 32-primitive chunk each pixel first builds a coverage bitmask, then shades only the primitives
      covering it, in order (bit-exact by construction). The critical path of a hot tile (Mono's hair: ~800 tiny
      triangles in one 16x16 tile) is now the per-pixel overdraw instead of every primitive in the tile.
    * Exact triangle/tile binning on the CPU (tiles whose pixel centers all fail an edge are not binned): shadow-volume
      pass 1.27M -> 0.42M (tile, primitive) pairs per 10 fields, 26 -> 18 ms.
    * `PS2X_GS_GPU_PROF=1` prints GPU timestamps per dispatch kind, the slowest dispatches with their batch shape, time
      per render target, flush reasons and upload hazards. Measured and not worth it: swizzle tables in shared memory
      (lookups are ~5% of the main scene), SSBO-only barriers.
    * All of it ran in-game (shrine, New Game intro 3600-3900 where 8x8 tiles had hung the driver) without a stall.
* **VU1 and GIF on their own threads (MTVU, eighth session, default)**: `ps2x::asyncvif` runs VIF1 and GIF PATH3
  jobs in order on a "VUThread" and GIF packets on a "GIFThread" (chunks of 256 KB). The EE waits for both
  only when it touches VIF1/GIF registers, VU1 memory or GS privileged registers. DMA completion stays
  immediate, so interrupt timing is no longer deterministic when this is on. `PS2X_MTVU=0` restores the
  deterministic serial path (goldens and `Tools/golden_run.py` use it); `PS2X_MTVU_STATS=1` prints fields/s,
  VU thread busy % and VU ms per field.
* **Guest call unwinding fix (eighth session)**: when a scheduler checkpoint unwinds generated code,
  `ctx->pc` holds the entry of the deepest pending call. `dispatchGuestBranch` treated `ctx->pc == callee
  entry` as "the callee returned without setting pc", so in recursive code (collision BVH walk
  `sub_01223E70` <-> `sub_01229658` <-> `sub_01223FE8`) an outer level resumed after its `jal` with the inner
  frames' `$sp`. The scheduler now flags an unwind in progress (`EeScheduler::unwinding()`), cleared before
  it re-dispatches. This crashed New Game at field 1739 (JALR to 0x80000000 in
  `ClipCollisionDirectCallBackObjAry`).
* **VCLIP (VU0 macro) semantics**: the recompiler judged against the signed `w` and swapped the +/- flag bits;
  it now matches the VU1 interpreter (|w|, +x = bit 0, PS2 denormal handling). This changes EE culling of
  objects behind the camera; the cutscene golden was re-recorded (57,499 hashes; the interpreter-only VU1 run
  records the same file).
* **DMA chains longer than 4096 tags**: the runtime's chain walker stopped after 4096 tags and silently dropped
  the rest. The canyon shots of the opening cutscene (from field ~2520) build VIF1 chains of 4,190-4,600 tags;
  the dropped tail held the letterbox bars and the font re-upload (IMAGE GIFtag in one DIRECT, 8 KB of pixels
  in the next), so the GIF stayed in IMAGE mode and swallowed the next frame's first 8 KB of A+D packets,
  including `FRAME_1 = 0xE8`: the whole scene went to FBP 0 and the screen showed dark-blue silhouettes and
  no letterbox. The cap is now 2^20 tags with a log line if it is ever reached.
* **VU0 macro audit and fixes**: `Tools/vu0_corpus.py <out.inc>` extracts every distinct COP2 operation the
  game uses together with its generated C++ (752 encodings); configuring ps2xTest with
  `-DPS2X_VU0_AUDIT_CORPUS=<out.inc>` builds `vu0_audit`, which runs each one on random inputs against the VU1
  interpreter (upper ops as micro upper words, lower ops as micro lower words) and reports vf/ACC/Q/clip/VI/R
  differences. Found and fixed: `VRNEXT` never wrote its destination and `VRINIT`/`VRXOR` used an invented
  generator (now the PS2 LFSR on bits 4/22, R in [1,2), broadcast to all lanes; `CTC2 R` keeps the mantissa),
  so `iosGetRandom` (68 callers: particles, water, AI, camera shuffle) returned stale register contents;
  `VFTOI` returned 0x80000000 on positive overflow (now 0x7FFFFFFF); `VABS` kept denormals. Remaining
  differences are 1-4 ulp rounding in multiply-adds (host SSE under round-toward-zero, as in PCSX2), overflow
  near FLT_MAX, exponent-255 inputs, and `VRSQRT` by -0 returning -MAX (PCSX2 does the same).
* **VU0 R register shared across threads**: guest threads and handlers each have their own VU0 registers;
  the R register now follows the scheduler's shared copy (`EeScheduler::loadSharedVu0Random`), because
  thread contexts start with R = 0, where the LFSR never advances. Sharing the whole VU0 file was tried and
  rejected: our scheduler switches threads between function dispatches, where the PS2 would not, and leaked
  vector registers produced a haze over the clouds. With the working generator the cloud shot draws 264-270
  mist particles per frame, as PCSX2 does (261-268; 233-240 before). The cutscene golden was re-recorded for
  this intended change (58,833 command hashes); the other four goldens are unchanged.
* **VU0 macro VU-memory ops**: `VSQI`/`VSQD` stored `vf[it]` at `vi[fs]` (fields swapped) and `VLQI`/`VSQI`/
  `VLQD`/`VSQD`/`VILWR`/`VISWR` addressed EE RAM (`vi << 4` from address 0) instead of VU0 data memory.
  KERNEL's matrix stack (`iosPushCurrentMatrix`/`iosPopCurrentMatrix`: `vsqi vf1..vf4, (vi15++)` and
  `vlqd vf4..vf1, (--vi15)`, current matrix in `vf1`-`vf4`) therefore popped garbage, so every node below
  a push/pop in a character hierarchy got a broken world matrix: the hawk's bones 2-10 had zero rotation
  rows and a quaternion in row 3, which the skinning microcode turned into streaks. The ops now use VU0 data
  memory (`(vi & 0xFF) << 4`, 4 KB) with the hardware field layout, `VILWR`/`VISWR` address qwords per
  field, and VI0/VF0 are never written (`VIADD`/`VISUB`/`VIADDI`/`VIAND`/`VIOR`, `VILWR`, the
  post-increment/pre-decrement and loads into VF0). Only the two matrix-stack functions use these ops in
  this game; all five goldens are unchanged. Found with `SOTC_WATCH_WRITE` on the hawk's node-matrix array.
* **EE FPU / VU0 macro semantics**: saturation instead of Inf/NaN, EE divide by zero, `sqrt(|x|)`,
  `VRSQRT = fs/sqrt(|ft|)`, `CVT.W.S` saturation; MXCSR round-toward-zero + FTZ/DAZ on the game thread.
* **GS COLCLAMP and triangle fill rule (tenth session)**: the blend now honours COLCLAMP: with COLCLAMP = 0 the
  blended colour keeps its low 8 bits (wraps) instead of saturating. The game draws the characters' shadow volumes by
  adding 1 for front faces and 255 (-1) for back faces into FBP 0x178 with COLCLAMP = 0, ALPHA test NEVER/AFAIL
  FB_ONLY and ZTST GEQUAL, then uses the non-zero pixels as a shadow mask; with saturation a back face drawn before
  its front face left 255 instead of 0, so a hard-edged hull-shaped shadow followed the rider (canyon ~2740, courtyard
  arch ~7650-7700, passage ~8600-8900). Found by playing PCSX2's exact GS stream through our GS (`gs_replay
  --gsdump`, with `GS_REPLAY_SNAPSHOTS=first:last:every` VRAM snapshots mid-frame) and inspecting the count buffer.
  Triangles now use the GS rule: vertices in 12.4 fixed point, samples at integer pixel positions, exact integer edge
  functions with a top-left tie rule (every pixel on a shared edge belongs to exactly one triangle), barycentric
  weights from the same integers (`gs_triangle_rules.h`, used by the CPU fast path, CPU fallback, reference rasterizer
  and, with `imulExtended`/`uaddCarry` 32-bit arithmetic, the GPU shader). The old rule sampled at `x + 0.5` and
  accepted pixels within 1e-4 of any edge, so shared edges were drawn twice. A first GPU version evaluated the edges
  in double precision per pixel (with a double division); on a GeForce that was slow enough in heavy scenes for the
  OpenGL driver to abort the process (fail-fast code 7 from the driver inside raylib's frame presentation, at random
  fields). Tests: a COLCLAMP wrap/saturate test; the triangle-fan hole test now reads pixels through the CT32 swizzle
  (it read VRAM linearly and only passed because the old rule also covered the border row); the two STQ tests shift
  their triangle by half a pixel so the sampled point is the same as before. Goldens re-recorded (two runs identical).
  `Port/src/main.cpp` installs a terminate/SIGABRT handler that prints a symbolized stack; fail-fast crashes need a
  debugger (a small Win32 debug-API script was used).
* **EE FPU rounding like PCSX2 (tenth session)**: `ADD.S`/`SUB.S`/`ADDA`/`SUBA`/`MADD`/`MSUB`/`MADDA`/`MSUBA`
  now align their operands like the PS2 adder (PCSX2 `FPU_ADD_SUB`): when the exponents differ by 25 or more the
  smaller operand becomes a signed zero, otherwise its mantissa bits below the shifted-out position are cleared
  before the (round-toward-zero) add, so `1 - tiny = 1.0` instead of `0.99999994`. `DIV.S`, `SQRT.S` and
  `RSQRT.S` round to nearest, as PCSX2 does (`recDIV_S`/`recSQRT_S`/`recRSQRT_S` switch MXCSR). Found through
  the game's boot-time sin/cos table (`InitTableSinCos`, 16,385 floats at `0x1FE0C0` from the game's own `sinf`/
  `cosf`): 14,859 entries were 1 ulp lower than in PCSX2 and sin(90 deg) ended at `sqrt(2^-23)` instead of 0; every
  table-based rotation (e.g. the mist emitters' quaternions) was 0.04 deg off. The table now matches PCSX2 bit for
  bit, the mist emitter structs match PCSX2's RAM, and the language-menu text vertices match a PCSX2 GS dump
  (52/52, was 32/52; e.g. x = 2028.00 instead of 2027.94). All goldens were re-recorded for this intended change.
* **LQ/SQ/LQC2/SQC2 ignore the low 4 address bits** like the EE (the masking lives in the runtime's 128-bit
  read/write helpers, so no regeneration was needed). No golden changed.
* **GS sprite coverage rule (tenth session)**: sprites now cover pixels `ceil(x0)` to `ceil(x1) - 1` (and the same
  for y), with attributes evaluated at the integer pixel position `t0 + (x - x0) * dt/dx`, fractional FST UVs
  (`UV / 16`, the 4 fraction bits used to be dropped) and the exact XYOFFSET, as the GS and PCSX2's software
  renderer (`DrawSprite`) do; zero-width sprites draw nothing. The old rule truncated the vertex coordinates and
  sampled at `x - trunc(x0) + 0.5`, so the game's `-0.5`-offset post-process sprites (display copy `-0.5..511.5`,
  bloom downsamples `-0.5..255.5` with `u = 0..512`) missed their last column and each bloom level was shifted by
  up to a texel. That was the faint bright/dark band at the right edge of the opening cutscene (a black last display
  column plus a ~12-pixel falloff) and part of the wider bloom halo. CPU fast path, CPU fallback, reference
  rasterizer and GPU shader share `gs_sprite_rules.h`; seven unit tests that drew zero-size sprites to mean
  "one pixel" now draw real 1x1 sprites. Goldens re-recorded (two runs identical); `ps2x_tests` 472 (new tests
  for the FPU alignment, round-to-nearest div/sqrt, LQ/SQ masking and the sprite rule), IOP ctest 5/5.
* **Gameplay reached (twelfth session)**: New Game -> intro -> the shrine with Wander under player control. Both routes
  work: letting the intro play out (the Dormin dialogue ends at field ~30,000 and hands over to gameplay at the altar;
  pad script `...,1250v:start,1450v:start,1550v:cross,1700v:cross`) and skipping it with Start (e.g. `4000v:start`,
  gameplay at field ~4150, as in PCSX2). Tested with scripted input: walking and running (left stick), stairs, the
  camera (right stick; same response as PCSX2 for the same input, including the game's reversed default axes), drawing
  and swinging the sword (Circle / Square, weapon HUD and stamina bar), holding up the sword (hold Circle, focus marker as
  in PCSX2; the altar is indoors, so no light beam there), whistling for Agro (Triangle; she gallops in), and riding
  (tested by the user). Speed in the shrine: 25-30 fields/s, VU-thread bound (see Known blockers).
  * **Skip crash fixed (`dispatchGuestBranch`)**: pressing Start during the intro jumped to `0x01D1FF90` (heap data). The
    script system's `execSCRBaseFunc` calls `SCRFuncSubFunc`, which tail-jumps (`j`) back into `execSCRBaseFunc`. The tail
    jump unwinds the host frames with `ctx->pc = 0x1467A00`, which is exactly the entry of the function
    `updateScriptDataSystem` had called, and the "callee returned with its entry PC = implicit return" heuristic in
    `dispatchGuestBranch` resumed `updateScriptDataSystem` with the callee's `$sp`. Its later prologue overwrote a live
    saved `$ra` (found with `SOTC_WATCH_WRITE` on the clobbered slot and the new branch trace). The heuristic now only
    applies when no guest jump was dispatched inside the callee (per-thread jump serial). Test: "dispatchGuestBranch call
    does not treat a tail jump to the callee entry as a return". Goldens unchanged (all five SAME), `ps2x_tests` 474,
    IOP ctest 5/5.
  * **Pad script sticks**: steps accept `lleft`/`lright`/`lup`/`ldown`/`rleft`/`rright`/`rup`/`rdown` (full deflection,
    combinable with buttons: `5900v:lup+cross:20`).
  * **New diagnostics**: `PS2X_BRANCH_TRACE=n` keeps a per-thread ring of the last n guest calls/returns/jumps/scheduler
    dispatches (source, target, `$sp`, `$ra`) and dumps it on a missing branch target (`PS2X_BRANCH_TRACE_FILE=path`,
    else stderr); `PS2X_DUMP_RAM_ON_MISSING=path` writes guest RAM at that moment; `SOTC_WATCH_WRITE` now logs the host
    call stack (the generated guest functions) and `SOTC_WATCH_VALUE=hex` logs only writes of that value.
* **Tutorial hints (thirteenth session)**: after the intro skip, "Press X to jump" and "In a sunlit place, hold up the
  sword using O..." appear natively with the same pad script as in PCSX2 (`4000v:start,4150v:cross,4300v:cross`; native
  captions start at 4145/5143/5408, PCSX2 4205/5211/5481: the post-skip load is ~60 fields faster natively),
  and neither hint appears without the Cross presses, as in PCSX2.
  * **Root cause: no SPU2 voice model.** The hints are not the hint-work system (`createHintWork` & co. are never used
    in PCSX2 either): they are script event demos. `SCRFuncStartEventDemo(GameModeCheck)` ->
    `createScrEventDemoManager` (`0x146C9A0`) fills one of two slots at `0x1307498` (stride 0x58) and starts the event
    thread `sub_0146C218`, which plays a BGM (`bgmStandby` with end callback `sub_0146CC30`), shows its caption through
    `sub_0146C0E8` -> `DisplayCaptionTableBlock`, and only frees the slot after the BGM end callback has set
    `+0x1C = 1`. The "Now be on thy way" event's music never ended natively, so its slot stayed busy and every later
    event (the hints) failed to start. The BGM ends when the ADPCM stream runs out: `sg2iop_driver`'s stream thread
    polls every voice's NAX through `sceSdGetAddr` to see which half of each double buffer is playing, and our IOP had
    SPU2 as a plain register store, so NAX never moved.
  * **`IopSpu2`** (`ps2xIOP/src/emulator/devices/iop_spu2.*`): both cores' registers with 16-bit access (the generic
    hardware path turned 16-bit writes into 32-bit read-modify-writes, which would have keyed on unrelated voices),
    2 MB SPU2 RAM filled by the real DMA channels 4/7 (IOP RAM <-> TSA, not in AutoDMA mode) and PIO (`0x1AC`), and
    per-voice playback: KON/KOFF, pitch-rate sample counter at 48 kHz (768 IOP cycles per sample, advanced lazily to
    the IOP cycle on each access, so it is deterministic), NAX/LSAX/ENDX with the ADPCM block loop flags and the ADSR
    envelope (ENVX, which the driver reads for voice allocation), following PCSX2's SPU2 voice logic. No IRQ address
    and no mixing yet. The BGM now ends at field 5126 (PCSX2 5201; it plays 1006 fields vs 1018). Goldens unchanged
    (all five SAME), `ps2x_tests` 474, IOP ctest 6/6 (new `ps2_iop_spu2_tests`), shrine speed unchanged.
    The naturally played intro (no skip) still matches PCSX2 scene for scene up to field 26,000 (captures every 1,000
    fields, same Dormin lines within a few hundred fields).
  * **PCSX2 call logging via PINE code caves** (scratch `pcsx2_hook.py`): writes `j cave` + `nop` over a function's
    first two instructions (neither may be a branch or PC-relative); the cave saves t0-t2 on the stack, appends
    {id, VBlank counter, a0-a3, ra, a word or a pointer chain like `[[a0+4]+8]`} to a ring at `0x00FF0000`
    (unused in both runs), replays the two instructions and jumps back. Found the event demo path in two runs.
* **Diagnostics**: game TTY (`[GAME]` = guest stdout), categorized logs (`SOTC_TRACE`), stack watchdog
  with VSync rate and EE thread/semaphore snapshot (`SOTC_WATCHDOG`, `SOTC_WATCHDOG_THREADS`),
  sampling profiler (`SOTC_PROFILE=delay:seconds`), guest call tracer (`SOTC_TRACE_CALLS`), thread
  tracer (`SOTC_TRACE_THREADS`), scripted pad input (`PS2X_PAD_SCRIPT=start:button[+button][:hold],...`; `start` in seconds, or in
  VSync fields with a `v` suffix, e.g. `290v:cross,375v:cross,770v:down,800v:cross` reaches the SCEE logo
  without a memory card; field-timed steps hold 10 fields by default),
  RAM dumps at a function entry with an optional memory condition
  (`SOTC_DUMP_RAM_AT=addr:file[:when=addr=value]`), hardware write watchpoints on guest RAM
  (`SOTC_WATCH_WRITE=addr[:len],...`, up to 4, len 1/2/4/8: debug registers on the GameThread; each writer is
  logged with the VSync count and the symbolized host function and line, i.e. the generated guest function),
  missing guest functions stop the run.
* **GS diagnostics**: `PS2X_GS_TRACE=firstVsync:count:file` writes every draw (`D`, full context
  state + vertices), transfer (`T`), image payload (`I`, hex) and field (`V`, PMODE/DISPFB/DISPLAY) and
  dumps raw VRAM at the start of the window to `file.vram`; `SOTC_DUMP_RAM_AT` also takes `:after=seconds`
  and `SOTC_TRACE_CALLS` prints `$f12`-`$f15`; `PS2X_GS_TRACE_DUMP_DRAW=n` also dumps VRAM
  after the n-th traced draw to `file.vram.draw`. `Tools/gstrace_summary.py <trace> [vsync]` collapses
  draws; `Tools/gs_vram.py` decodes CT32/PSMT4 regions of a VRAM dump to PNG.
* **GS recordings and golden replay**: `PS2X_GS_RECORD=first:count:file[;first:count:file...]` records
  every backend command (draw batches bit for bit, CLUT loads, transfers, uploads, TEXFLUSH, MXCSR
  changes) plus the backend state and full VRAM at the start and at each field; `PS2X_GS_RECORD_HASH=1`
  adds a VRAM hash after every command, `PS2X_GS_RECORD_CHECKPOINTS=i,j,...` full checkpoints after
  given commands. The golden set lives in `Analysis/oracle/gs_golden/` (git-ignored): `menu_language`
  (fields 250-254), `menu_hz` (320-323), `loading` (540-543), `logo` (900-904), `cutscene` (1360-1364),
  recorded with pad script `290v:cross,375v:cross,770v:down,800v:cross`. `ps2x_tests` replays them through
  the old rasterizer (kept as `ps2xTest/gs_reference`), `GSCpuBackend` and the threaded GS
  (`PS2X_GS_GOLDEN_DIR` overrides the folder). `ps2xTest/tools/gs_replay <file.gsr>` checks one recording
  (`--backend cpu|ref|thread`, `--no-compare --repeat N` for timing, `--profile` per draw state with
  fallback counts, `--sample` for a line-level sampling profile, `--lockstep` for the first command where
  a backend diverges from the reference, `--text out.txt` for the `PS2X_GS_TRACE` text format;
  `PS2X_GS_GOLDEN_DUMP=prefix` writes expected/actual VRAM on a mismatch). The text trace alone cannot
  drive an exact replay (coordinates are printed with two decimals).
  `SOTC_PROFILE_THREAD=GSThread` makes `SOTC_PROFILE` sample the GS thread instead of the game thread.
  Both `gs_replay --text` and `Tools/gsdump.py` print the per-vertex fog as `f=`; `SOTC_TRACE_CALLS` also prints
  the VU0 R register. `PS2X_GS_RECORD` paths are relative to the game's working directory (the repo root) and a
  missing directory fails silently, so pass absolute paths. The recorder's per-field checkpoints (`Op::Field`)
  can hold stale VRAM (seen with both the GPU and the CPU renderer late in the cutscene: the scene buffer still
  showed the cloud shot), so do not trust checkpoint VRAM or the first replayed field of a window that starts
  mid-game; the command stream itself is fine. To be investigated.
  `Tools/gsdump.py <dump.gs> --export out.bin` converts a PCSX2 GS dump into VRAM + GS register state + the raw
  GIF stream + per-vsync display registers, and `gs_replay --gsdump out.bin [cpu|ref|gpu] [dir]` plays it through
  our GS and writes one presented PNG per vsync plus the final VRAM. Playing PCSX2's exact stream through our
  rasterizer and comparing with PCSX2's own output separates GS differences from EE/VU differences (the tower shot
  at native ~7790 reproduces PCSX2's software renderer: mean 101.5 vs 100.5). Take such dumps with PCSX2's
  software renderer (`Renderer = 13` in `inis/PCSX2.ini`): the hardware renderer's dumps have empty render targets
  in VRAM.
  `SOTC_PROFILE` takes several windows (`8:3,31:10`), samples raw PCs (~450 samples/s) and reports self
  time per function and per source line, inclusive time and the callers of the top functions.
  `SOTC_TIMELINE=file` logs the wall time of every field (`SOTC_TIMELINE_WATCH=addr,...` adds guest words);
  `PS2X_TRACE_SCHED=first:last:file` logs every guest thread block/wake/switch/invocation with field and
  EE cycle.
* **Boot timing tools**: `Tools/measure_boot.py native|pcsx2 <dir>` runs the game or PCSX2 with the pad
  script, captures the window (PrintWindow, works when occluded) and, for PCSX2, drives the pad over
  PostMessage at the same guest VBlank counts (cross, circle, triangle, square, start and the D-pad map to PCSX2's
  keyboard bindings) and polls over PINE the game's VBlank counter
  (`0x1DC9D8`, native field - 35) and libcdvd's pending read (`0x130A80` lsn/sectors, busy flag
  `0x1309B4`). `Tools/boot_milestones.py <dir> native|pcsx2` classifies frames into milestones,
  `Tools/boot_phases.py <dir> f0,f1,...` prints fields/s between native fields,
  `Tools/golden_run.py <dir>` records the five golden windows in a game run and
  `Tools/gsr_compare.py a.gsr b.gsr` compares checkpoints and per-command hashes (struct padding in
  Submit records differs run to run and is ignored). `0x1DC7AC` looks like a VBlank counter but counts
  `iosCheckDrawFinish` polls.
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

* Speed after the fifteenth session (fields/s, same machine, start of session in brackets): shrine gameplay 48-49.5
  (32), clouds 50 (50), canyon 49.3 (40), riders 49.7 (37.5), forest/ruins 48.7 (39), towers 41.7 (39), passage 41.2
  (40). The shrine is no longer GPU bound (the GS thread never waits for the GPU; it is idle ~55% of the time). Limits
  now: the VU thread (87-91% busy, ~18 ms of VU work per field in the shrine, ~21-22 ms in the towers/passage) and the
  EE thread (~95% busy in the shrine). DMA chain buffers are now recycled through the async VIF spare pool (each VIF1/GIF
  chain transfer allocated a fresh buffer of the largest size seen and the VU thread freed it). EE-side changes: `__floatdisf` HLE (`Port/src/sotc/hle/libgcc.cpp`: libgcc's
  soft-double int64->float routine was 4.6% of the EE thread; the native version truncates like the guest routine,
  0 mismatches over 1M calls with `SOTC_VERIFY_LIBGCC=1`, and charges 1,416 EE cycles), IOP batch 128 -> 1,024 IOP
  cycles (the IOP bookkeeping was ~4% of the EE thread), the branch-trace switch read without a thread-safe static
  guard on every call. VU side: `resetScheduler` clears its pipelines with `memset` (~3% of VU1 time). The cutscene
  golden was re-recorded after the HLE (EE cycle accounting shifts a cloud animation by a fraction; at most 2 pixels
  differ by more than 8 per field); the other four goldens are unchanged. `ps2x_tests` 477 (the "SifInitRpc does not
  reset the running IOP" test now advances 16,384 EE cycles, past one IOP step), IOP ctest 6/6.
* Speed after the fourteenth session (fields/s, MTVU + GPU renderer, RTX 3060 / Ryzen 5 5500; baseline at the start
  of the session in brackets): clouds 49.5 (42.6), canyon 40 (27.9), riders 37.5 (27.5), forest/ruins 39 (32.8),
  towers 39 (17.1), passage 39.6 (15.8); shrine gameplay ~32 (25-30). Measured with the VU1 timing model and the
  experimental GPU changes that were reverted afterwards (see below); VU1-program time per field in the trace bench
  fell 35-40% (passage 19.3 -> 14.2 ms, towers 72 -> 47 ms, shrine 18 -> 14 ms). VU1 load per field
  (bench cycle counter): towers 11.1M, passage 4.0M, shrine 4.1M VU1 cycles; a PS2 VU1 does 5.9M per field. One
  entry (image 2048debd, pc 0x05B0: ~12 vertices per MSCAL, 23.6k MSCALs per field in the towers) is 60-70% of all
  VU1 work. Remaining limits: the VU thread in the canyon/riders/towers/passage (~80% busy), and the **GPU GS
  renderer in the shrine** (~70 ms GPU per game frame, 14 presents/s). GS GPU findings (replay of a 10-field shrine
  recording, `PS2X_GS_GPU_PROF=1` profiler kept in scratch, not committed): raster 286 ms, uploads 29 ms, CLUT 11 ms
  per 10 fields; the main scene (FBP 0xE8) is split into ~82 dispatches per field, almost all by host->local
  uploads: first by palettes overwritten while a queued CLUT load still needs them (fixable by dispatching queued
  CLUT loads early), then by textures streamed into pages the pending batch still samples (write-after-read; would
  need copy-on-write pages); removing all upload flushes cuts raster ~33%. The shadow-volume pass into FBP 0x178
  (~5,700 full-screen triangles, 15-18 ms per dispatch in the intro) is the single most expensive dispatch.
  8x8 tiles with shared-memory primitive staging were 11% faster in the replay but made the NVIDIA driver hang/abort
  in-game (New Game intro ~3620-3666, froze the user's PC); reverted. fp64 Z interpolation and per-thread
  framebuffer/Z caching were not the bottleneck.
* Speed after the eighth session (fields/s, default MTVU + GPU renderer, RTX 3060): boot, menus, loading,
  "No memory card" and logo 50 (paced); cloud cutscene from field 1215 ~37-45 (VU thread ~96% busy,
  ~25 ms VU work per field); canyon part of the cutscene from ~1790 ~23-25 (~45-50 ms VU work per field, ~20% in
  the exact FMAC path: `fmacExact<3>` 12.8% and `fmacExact<2>` 6.2% of the VU thread), riders/forest
  2100-5200 ~24-26. Neither the hawk fix nor the DMA/VU0 fixes changed the speed noticeably (runs vary by
  about 10%); after the tenth session's FPU and sprite changes: clouds (1250-1780) ~41, canyon 1800-2500 ~25,
  2500-5200 ~25, forest/ruins 5200-7900 ~27-29 (unchanged within noise); after the COLCLAMP/triangle-rule change: clouds ~45, canyon ~27, forest/ruins ~30, the passage after the ruins (8000-9500) ~17 (new VU1 programs there are probably not captured for the AOT compiler yet); title view (Start at 1250) 50;
  after the eleventh session (all 11 VU1 images of the cutscene compiled, exact-zero FMAC fast path, VU1 image
  cache, templated VIF unpack): clouds ~45, canyon ~29, riders 2500-5200 ~29.5, forest/ruins 5200-7600 ~35,
  towers 7600-7900 ~19 (was 12-17), passage 8000-9500 ~17.6. Everything is VU-thread bound (~95% busy); the
  exact FMAC path is gone from the profile; the passage spends ~41% in one compiled block (image 2048debd,
  block 5) whose cost is per-pair ready-cycle checks and flag-ring records;
  New Game loading and the intro (bridge, shrine, ruins) 50 up to field 3600 at least.
  Pad script for New Game: `...,1250v:start,1450v:start,1550v:cross,1700v:cross`.
* Opening cutscene (attract demo, no Start press): side-by-side captures against PCSX2's hardware and software
  renderers now match shot for shot (clouds, hawk, canyon, riders, forest, courtyard, the ruined towers that fade
  into white) within 1-2 grey levels of mean brightness. Findings of the tenth session:
  * **Pacing is identical**: the demo clock is the game's scaled frame time (`0x1477270`, from the EE T0 count via
    `iosGetTCount`, 0.0196 s per frame, one frame per field in both); `sub_013DF6C0` passes it to
    `ExecLwsDemoOrientManagerObj`. The camera position (`0x13018A0`) matches PCSX2 to 0.00 units with a constant
    offset: native field N shows what PCSX2 shows at N + 147 (native field numbering), from the first cutscene
    frame to the end. Native simply starts the cutscene 2.9 s earlier because the pre-cutscene load finishes sooner
    (disc model, see the timing notes). The earlier "35 growing to 185 fields" came from comparing at a guessed
    +35 offset. Not changed: matching PCSX2 here would mean slowing loading down.
  * **"Building partly disappears"** (fields ~7650-7950): the towers fading into white from the bottom is the game's
    own effect and PCSX2 does the same; with the wrong offset it looked like a native bug. What differed was the
    right-edge band and bloom placement (fixed by the sprite rule). The hard shading edges on the arched doorway at
    ~7650-7700 were the COLCLAMP shadow-volume bug and are fixed.
  * **Brighter lower tower wall at ~7790 (eleventh session): random mist, not a rendering bug.** Frames cut at the
    display copy (`D` to `tex0=1d00` into FBP 0) match PCSX2 command for command (16,761 of 16,983 lines identical):
    the ground haze (textures 0x2E00-0x2EC0) is identical including the clipped-edge alpha. The earlier "0x80 vs
    0x40 alpha" compared haze one frame apart, because the V (VSync) marker falls before the display copy in
    PCSX2's dump and after the scene in ours. What differs is the set of ~16 full-screen mist planes (texture
    0x2A00, depth-tested, alpha 0x02-0x0E) the game places at random depths and tints: at the same frame PCSX2 has
    planes at z 3026/4305/6541/8535..., we have 3955/5012/5182/8701... Each plane shows a hard edge where it cuts
    through the wall, and ours happen to wash out the lower wall (mean 105 vs 101). The random generator is VU0's R
    (`iosGetRandom`, LFSR, seeded once by `iosInitRandom`). Our R equals PCSX2's exactly at every savestate up to
    native counter 5177 (VU0 R is `VI[20]` of `vuMicroRegs` in `PCSX2 Internal Structures.dat`, 16-byte stride).
    At the shot change at counter 5319 PCSX2 drops one frame: the game's frame delta (`0x1477270`) is 0.0393 s
    instead of 0.0196 s at PCSX2 VBlank 5469, i.e. the first frame of the new shot takes two VBlanks on the PS2 and
    one fewer logic update runs. From then on our random sequence is exactly one frame (169 draws) ahead of
    PCSX2's. No disc read happens there (the shot's data streams in ~500 fields earlier), so the hitch is EE/VU/GS
    load, which our timing model does not charge (8 cycles per dispatch, 32 per loop, nothing for VU1/GS work).
    Reproducing PS2 frame drops would need real EE/VU/GS timing and would make the port drop frames it does not
    have to, so it is not done. The same frame explains the haze texture scroll being ~0.1% off in S. It does not
    explain the far mist billboards in the cloud shot (below), which happen while R is still in sync. Natively the rider/horse is also still drawn below the screen where PCSX2
    culls it (~26k off-screen vertices per field; invisible, costs time).
  * **Bright border from ~4350**: the sprite coverage rule (see above); gone.
  * **Mist haze in the cloud shot**: at the correctly aligned moment the mist's mean alpha matches PCSX2 (17.6-17.9
    vs 17.7-18.9; the earlier 12.8 vs 10.1 compared different moments). The particle lists in RAM are identical to
    PCSX2 (same 298 particles, same list nodes, same emitter state), and the near particles are drawn identically
    (same depths and alphas). The difference is the far mist emitter: natively it emits the same particles in the
    same order as PCSX2 but stops after ~16 visible billboards where PCSX2 draws ~27, so natively the clouds are
    slightly less hazy (std 9.7 vs 8.1). These billboards surround the camera and are clipped by the GL layer's EE
    clipper (`glEnd` -> `sub_011823F8` -> `sub_01182038`/`sub_01181800`, `vclip` + `cfc2 $vi18`, `div.s`)
    against a projection whose z and w columns are almost equal (`z = w - 0.096`), so the depth/clip decisions for
    them are extremely sensitive to rounding; not solved.
* Gameplay divergences found in the shrine (twelfth session), not blockers:
  * Tutorial hints: fixed in the thirteenth session (see Working).
  * **Weapon HUD flash after the skip** (PCSX2 shows HP/grip/weapon HUD at ~4215-4300; native does not): load timing,
    not the SPU2 cause. The HUD elements (`0x12FE6F0`, 4 x 0x10) are forced off by `playerlifebar_script` while the
    screen fade (`0x14774F0`) or the letterbox (`0x1477508`) is active. The life sprites start with animation id 1
    (bits 27-30 of the sprite word +8); the first `player_life_anim` call sees it and requests the HUD
    (`sub_014060D0(4)`). In PCSX2 the letterbox is gone at 4129 and the fade-in runs 4164-4214, so that first request
    lands when the HUD is allowed. Natively the post-skip load ends ~56 fields earlier: the fade-in starts at 4108 with
    the letterbox still at 0.21, the letterbox then retracts ~5x slower, the request (~4185) is cleared by the next reset
    and the animation is over when the HUD becomes allowed (4209). Needs disc timing closer to PCSX2, not a fix here.
  * One fail-fast crash (`0xC0000409`) at field 7664 of the naturally played intro, seen once while a second game
    instance was running; not reproduced in two further runs (one under the debug-API script). Possibly the GPU driver
    abort seen in the tenth session.
* Title view and New Game intro (re-checked in the eleventh session): the "SHADOW OF THE COLOSSUS" logo, the menu
  entries and the copyright line render, and the intro sky over the bridge is bright white with no magenta column
  at the right edge (both fixed by the tenth session's FPU/sprite/triangle changes).
* Speed after the sixth session (fields/s, native field ranges): boot 0-270 ~51, language/50 Hz menus
  ~43-45, loading 375-731 ~46, "No memory card" + logo ~41-47, black gap before the cutscene 1133-1329
  ~27, cloud cutscene still ~1.3 (VU1). Milestones (wall s / game VBlank count, PCSX2 in brackets):
  language menu 5.9 / 236 (7.4 / 224), "No memory card" 16.2 / 699 (15.9 / 649), logo 18.7 / 807
  (19.1 / 809), logo end 25.8 / 1097 (24.9 / 1099), first cutscene frame 33.0 / 1294 (29.5 / 1329).
  What limits the remaining phases is the game's idle thread (`sub_001A8068`, lowest priority): an endless
  `iosRecvMsg(queue, 0, 0)` poll that makes WaitSema/GetThreadId/SignalSema syscalls. Natively it runs
  ~51,600 iterations per field (~114 charged EE cycles each; syscalls cost 0 cycles in our model), while
  the BIOS kernel path is ~250 instructions for WaitSema/SignalSema and ~45 for GetThreadId, i.e. ~600+
  cycles per iteration on the PS2 (~9-10k iterations per field). It takes 60-70% of the game thread in
  the logo and pre-cutscene phases.
* Speed (fields/s, PAL target 50), before -> after the GS thread + new rasterizer (2026-09-24):
  loading ~18 -> ~25-28, language menu ~7.4 -> ~24.7, 50/60 Hz menu ~8 -> ~20.6, SCEE logo ~5 -> ~18-19,
  cloud cutscene ~0.9-1.0 -> ~1.2-1.4. The language menu now appears at ~10 s, the logo at ~45 s and the
  cutscene at ~77 s (was ~19 / ~95 / ~176 s). Replaying the recordings (Release build, 4 fields each):
  menu 0.165 -> 0.019 s, logo 0.53 -> 0.065 s, cutscene 1.38 -> 0.26 s. Remaining bottlenecks are no
  longer the rasterizer: on the cutscene the GS thread idles ~78% while the game thread spends ~88% in
  `VU1Interpreter::run` (cloud geometry microcode); on the logo the game thread waits ~67% for FINISH
  and the GS thread is ~72% busy; the ~4.5 fields/s phase between the 50/60 Hz menu and the logo
  (fields ~510-580) has an idle GS and >50% of game-thread time in C++ exception unwinding
  (`EeScheduler::blockCurrent` from `SleepThread`/`iosRecvMsg`).
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
| Gameplay speed | The shrine runs at 48-49.5 fields/s; VU thread 87-91% busy and EE thread ~95% busy (the GPU renderer is no longer the limit) | VU1 codegen (the paired raw/normalized VF locals spill heavily; per-FMAC safety check), XGKICK sync on every VU store, EE-side HLE of hot library routines |
| Emulation speed | Opening cutscene: clouds 50, canyon ~49, riders ~50, forest ~49, towers ~42, passage ~41; the towers/passage are VU-thread bound (~21-22 ms of VU work per field); 60-70% of VU1 work in one microprogram (image 2048debd entry 0x05B0) | more AOT codegen work measured with `sotc_vu1_bench` |
| Audio | `IopSpu2` models voice playback (addresses, loop flags, ENDX, ADSR, DMA/PIO into SPU2 RAM) but produces no samples | ADPCM decode + volume/pitch modulation/noise + reverb per core, mixed to a host audio backend fed at 48 kHz from the IOP cycle clock; SPU2 IRQ address |
| Rumble | Motor values reach `IopHost::padVibration`, but raylib's GLFW backend cannot drive rumble | host rumble backend (XInput/SDL) |
| FMV | FFmpeg disabled | decide decoder strategy |
| EE cycle accounting | 8 EE cycles per recompiled-function dispatch, 32 per loop back-edge, 0 per syscall; the PS2 kernel spends ~45-250 instructions per syscall | no longer a speed problem (the idle thread is skipped); charge syscall costs only if a timing difference shows up |

## Temporary hacks / modelled behaviour

| What | Where | Why | Exit criterion |
|---|---|---|---|
| FFmpeg disabled (MPEG → stub frames) | root `CMakeLists.txt` | avoid third-party prebuilt downloads during bring-up | FMV strategy decided |
| HLE bindings for SIF/file/CD/DECI2/TTY/MPEG/IPU | `Port/recomp/sotc.toml`, `Port/src/sotc/hle/` | runtime IOP bridge is API-level | revisit per subsystem against PCSX2 |
| Disc latency model: `sceCdRead` at EE-cycle granularity (3.5 MB/s, 20 ms seek), fio open/getstat/read and `sceCdSearchFile` in whole fields | `Port/src/sotc/hle/sce_fileio.cpp`, `sce_cdvd.cpp` | real reads block the caller; the game's thread interleaving depends on it (boot module addresses) | calibrate against PCSX2 if loading times matter; loading is now shorter than in PCSX2 |
| Idle thread skip | `Port/src/sotc/idle_thread.cpp`, `EeScheduler::skipIdleCycles` | the idle thread's `iosRecvMsg` poll cost 60-70% of host time | — (skips only when nothing else can run) |
| SCE fio calls without a VFS equivalent return SCE error codes | `Port/src/sotc/hle/sce_fileio.cpp` | no IOP FILEIO server; each call is logged | implement if the game uses them |
| SIO2 transfer latency: 1,000 + 1,200 IOP cycles per byte; SIF DMA completion 64 cycles + 1 per 4 bytes | `ps2xIOP/src/emulator/devices/iop_sio2.cpp`, `iop_emulator.cpp` | order of magnitude of a 250 kHz pad link | calibrate against PCSX2 if pad latency matters |
| IOP VBlank runs at NTSC 59.94 Hz while the game is PAL | `ps2xIOP/src/emulator/iop_emulator_const.h` | pre-existing; DS1O_D polls the pad per IOP VBlank | make the IOP VBlank follow the GS video mode like the EE side |
| IOP runs in batches of 1,024 IOP cycles (8,192 EE cycles, 28 us) | `ps2xIOP/src/emulator/iop_emulator.cpp` (`kIopBatchCycles`) | stepping the IOP per dispatch cost ~25% of loading time; 128-cycle batches still cost ~4% of the EE thread in gameplay | — (well below any observable latency; goldens unchanged by the step from 128) |
| `__floatdisf` (libgcc soft-double int64->float, `0x121D00`) runs natively | `Port/src/sotc/hle/libgcc.cpp` | 4.6% of the EE thread in gameplay | — (same results; `SOTC_VERIFY_LIBGCC=1` runs the guest routine and compares) |
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
* The game's TEX1.K for the menu text now matches PCSX2 (0xFCE); menu text positions also match since the EE FPU
  add/sub alignment fix.
* Solar flare: `solarFlare` (`0x01196EA0`) switches on `0x0128FE70` (1 -> `sub_01193A78`, 2 ->
  `sub_011952E0`; PCSX2 and native both have 2, colour d0c498). `sub_011952E0` projects the sun direction
  (`0x01296320`, z = sqrt(1-x^2-y^2), x40000) through `get_cur_camera_context()+0x5A0` and draws only if
  |x/w|, |y/w| < 1.07 and z/w > 0. Camera contexts: index at `0x012939B4`, table at `0x01334200`.
* Boot game flow (`st_gameflow` `0x01361858`): `languageMenuLoad` -> `bootChooseLangScript` -> wait
  `menuSelected` -> `bootSelectDisplayMode` -> `commonDataLoad` -> loop until `gcCheckStageLoadFinish`
  (`[0x014778AC] == -1`, set at the end of `st_reloadtask` `0x01367398`). Upstream `sceCdRead` still has an
  "alternative argument order" fallback that can write to arbitrary addresses on unresolved reads; worth
  removing when it bites.
* The GS rasterizer inherits the game thread's MXCSR (round toward zero + FTZ + DAZ, needed for the EE
  FPU). Its float barycentrics/UV interpolation therefore truncate, which produces horizontal stripes in
  the upper-right triangle of full-screen bilinear quads (logo scene sky). The golden recordings contain
  this behaviour; the GS thread and band workers replay the producer's MXCSR to keep it. The GS source
  files are compiled with `/fp:precise` (Release otherwise uses `/fp:fast`, which contracts multiply-adds
  differently per binary; the game build is RelWithDebInfo and never used `/fp:fast`).
* The GS texture page cache is intentionally stale until TEXFLUSH (tests in `ps2xTest/gs_cache`); a few
  draws per frame (bloom chain) really read stale texels, so any new rasterizer must model it exactly.
* Script event demos: `SCRFuncStartEventDemo`/`SCRFuncStartEventDemoGameModeCheck` -> `createScrEventDemoManager`
  (`0x146C9A0`; refuses while `[0x1479158] > 0`, when the event id is already running, or when both slots at
  `0x1307498` are busy: slot `+0` event object, `+0x18` BGM handle, `+0x1C` BGM done, `+0x38` caption index,
  `+0x54` caption object). Event thread `sub_0146C218`, caption thread `sub_0146C0E8`, BGM end callback
  `sub_0146CC30`. BGM slots: 3 x 0x34 at `0x12EA308` (`+8` state 2..5 = standby..playing, 6/8 = stopping, `+0x24`
  flags 1 play/4 stop, `+0x2C` end callback, `+0x30` its argument).
* `sg2iop_driver` streams ADPCM through double buffers in SPU2 RAM and tracks each stream by polling NAX
  (`sceSdGetAddr(0x2240 | voice << 1 | core)`) against the buffer halves (table at driver `+0x7248`, 2 x 24 x 0x24);
  it reads ENVX of all 48 voices (`sceSdGetParam(0x500 | ...)`) into its table at `+0x70B8`. No SPU2 IRQ handler.
* IOP import map: `sg2iop_driver` → libsd, sifcmd, sysclib, thbase; `DS1O_D` → sio2man, sio2d, dbcman;
  `MC2_D` → sio2man, sio2d, dbcman, secrman, cdvdman.

## Next priorities

1. Speed (target: paced 50 fields/s everywhere). The shrine (48-49.5) and the cutscene (49-50, towers/passage ~41-42)
   are VU-thread bound (87-91% busy) with the EE thread close behind (~95% in the shrine). VU side: the compiled
   blocks still spill (MSVC); the per-FMAC safety check; the d18c8dfa image (second hottest, ~13% of the VU thread);
   VIF unpack (~6%) and XGKICK packet handling (~5%). EE side: more HLE of hot libgcc/soft-float routines (check
   the profile with `SOTC_PROFILE_THREAD=GameThread`), `EeScheduler::accountCycles` (~5%). GPU: ~350 small transfer
   dispatches per 10 shrine fields remain (palette/texture streaming through fixed areas; would need write renaming).
   Validate every shader change in-game, not only in `gs_replay`. Check the game's 60 Hz mode too (4.9M VU1 cycles
   per field).
   User reports to follow up (fifteenth session): the right-stick inversion and the stick-center fix (Agro drifting
   right) need the user's confirmation; the first colossus shows elongated geometry around an otherwise fine body
   (probably the fur; not reproduced yet: reaching it needs a ride plus a cliff climb; the game's
   `gameflowSetPos`/`gameflowSetJumpSekiban` (save-stone jumps) might allow a scripted teleport; then a GS recording
   and `PS2X_VU1_VERIFY=1` / new VU1 images captured with `PS2X_VU1_CAPTURE`). Then leave the shrine (light beam outdoors, the plains, the first colossus), capturing new VU1
   images (`PS2X_VU1_CAPTURE`) and regenerating `Port/generated/vu1`. Scripted events that wait for their music now
   progress (SPU2 voice model); watch for other events waiting on sound signals (`bgmScriptRecvSoundSignal`).
   The opening cutscene's remaining differences (tower-wall mist, far mist billboards, haze scroll) come from the
   frame the PS2 drops at the shot change at counter ~5319 (see Partially working); only a timing model that
   charges VU1/GS work would reproduce them. Still open: the far mist billboards in the cloud shot (~16 vs 27,
   GL-layer EE clipper). VU0 macro add/sub do not get the PS2
   operand alignment (PCSX2's microVU does not either). The GPU backend's `SnapshotVram` should read render targets
   back so GPU recordings get correct checkpoints.
2. Compare the opening cutscene with a PCSX2 GS dump draw for draw (clouds, hawk, cliffs); re-check the
   menu against PCSX2 now that the solar flare is gone. The faint horizontal stripes on full-screen
   bilinear quads come from the rasterizer's float barycentrics under the EE's round-toward-zero MXCSR
   (see discoveries); fixing them is a deliberate output change (e.g. fixed-point GS interpolation checked
   against PCSX2) and needs new golden recordings.
3. VU0 registers other than R are per context (each thread/handler has its own copy); on the PS2 they are
   shared. Sharing them all needs thread switches only where the PS2 makes them (see the haze note).
4. Memory card: verify the port can load a PCSX2-created save.
5. Audio: mixer and host output on top of `IopSpu2` (the voice model is there).
6. Sub-field timed waits in the scheduler; calibrate disc timing against PCSX2; IOP VBlank at the PAL rate.
7. argv, kernel object ID numbering.
