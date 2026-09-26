# Progress

Last updated: 2026-09-25 (EE FPU add/sub alignment and round-to-nearest div/sqrt, GS sprite coverage rule, LQ/SQ
alignment, opening-cutscene alignment with PCSX2; tenth session). Target: SCES-53326 v1.00 (see `GAME_BUILD.md`).

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
    and the (image, entry PC) pairs (`entries.txt`). `Analysis/oracle/vu1_capture/` now holds 8 images and 66
    entries (boot, cloud/canyon cutscene, title view, New Game intro up to field ~3700). New images or entries
    run interpreted until they are captured and the code is regenerated.
  * `ps2xTest/tools/vu1_recomp <capture-dir> Port/generated/vu1` (built in `C:/tmp/bt`, output git-ignored)
    emits per-op C++ per image: loop-aware blocks of up to 128 pairs, gotos for in-block branches, delay-slot and
    E-bit copies. Registers are written immediately with exact per-lane ready times; MAC/status/clip flags go
    through lazy FIFO rings and are folded on read; XGKICK progress is caught up lazily. FMAC ops take a fast
    path when every destination lane is safely normal (no clamping, no flag other than sign, no cancellation)
    and fall back to the exact double-precision path otherwise. Straight-line segments get a statically
    scheduled copy (entry checks for operand readiness, cycle snapshots instead of per-pair checks).
    `sotc_vu1_code` is built with `/arch:AVX2`.
  * `PS2X_VU1_VERIFY=1` runs every VU1 program twice (compiled, then the interpreter on a copy) and compares
    registers, pipeline state, VU memory and XGKICK packets (0 mismatches over 4.9M runs up to field 1785);
    `PS2X_VU1_RECOMP=0` disables the compiled code.
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

* Speed after the eighth session (fields/s, default MTVU + GPU renderer, RTX 3060): boot, menus, loading,
  "No memory card" and logo 50 (paced); cloud cutscene from field 1215 ~37-45 (VU thread ~96% busy,
  ~25 ms VU work per field); canyon part of the cutscene from ~1790 ~23-25 (~45-50 ms VU work per field, ~20% in
  the exact FMAC path: `fmacExact<3>` 12.8% and `fmacExact<2>` 6.2% of the VU thread), riders/forest
  2100-5200 ~24-26. Neither the hawk fix nor the DMA/VU0 fixes changed the speed noticeably (runs vary by
  about 10%); after the tenth session's FPU and sprite changes: clouds (1250-1780) ~41, canyon 1800-2500 ~25,
  2500-5200 ~25, forest/ruins 5200-7900 ~27-29 (unchanged within noise); after the COLCLAMP/triangle-rule change: clouds ~45, canyon ~27, forest/ruins ~30, the passage after the ruins (8000-9500) ~17 (new VU1 programs there are probably not captured for the AOT compiler yet); title view (Start at 1250) 50;
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
    right-edge band and bloom placement (fixed by the sprite rule). Remaining: the light shaft between the towers is
    slightly wider natively, and near-camera ground-haze polygons (texture 0x2E40, ALPHA 0x44) are too opaque where
    they are clipped: at the clipped edge PCSX2's vertices carry alpha ~0x40 (interpolated) while ours carry 0x80
    (the outside vertex's value). This shows as a brighter lower tower wall at ~7790 (the hard shading edges on the
    arched doorway at ~7650-7700 were the COLCLAMP shadow-volume bug and are fixed). PCSX2's GS stream played through
    our rasterizer matches PCSX2 on the same frame (compare the player's field N+1 with PCSX2's field N: the first
    presented field of a dump is PCSX2's own composite), so the difference is in the GS commands produced natively. Neither EE clipper
    (`sub_01182038`/`sub_01181800`) interpolates these polygons (instrumented: no calls in that field), so the
    clipping happens in the VU1 microcode of the GL path (`glEnd` -> PATH1 packet); next step is to capture that VU1
    program's input for one of these polygons and compare its clipped output with PCSX2 (e.g. VU1 Q/DIV pipeline
    timing or a flag-dependent branch). Natively the rider/horse is also still drawn below the screen where PCSX2
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
* Title view: the shrine and bridge render, but the "SHADOW OF THE COLOSSUS" logo, the menu entries and the
  copyright line that PCSX2 shows are missing (unchanged by the matrix-stack fix). The New Game intro sky is purple where PCSX2 is bright white,
  and a thin magenta column shows at the right edge.
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
| Emulation speed | The opening cutscene is VU1-bound: ~37-45 fields/s in the clouds, ~23-26 from the canyon on; everything else measured so far (title view, New Game intro) runs at the paced 50 | find which results send ~20% of the canyon's FMACs to `fmacExact` (images `2048debd5c78af0a` and `d18c8dfaae098293`) and give them a bit-exact fast path (check with `PS2X_VU1_VERIFY=1`) |
| Audio | `sg2iop_driver` drives SPU2 through LIBSD imports; runtime has no IOP-side SPU2 | SPU2 register model on the IOP side feeding a host mixer |
| Memory card | MC2_D now completes its SIO2 transfers, but ports 2/3 answer "no device"; the game shows "No memory card inserted" (Continue works) | memory-card device on SIO2 ports 2/3 (next to `VirtualDualShock2`), backed by a host file; compare the post-language-menu screens with PCSX2 |
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
* IOP import map: `sg2iop_driver` → libsd, sifcmd, sysclib, thbase; `DS1O_D` → sio2man, sio2d, dbcman;
  `MC2_D` → sio2man, sio2d, dbcman, secrman, cdvdman.

## Next priorities

1. The missing title logo/menu text and the New Game intro colours (re-check both first: the FPU and sprite fixes
   may have changed them); then play on from the intro into the first area and fix what blocks or slows it.
   Capture new VU1 images on the way (`PS2X_VU1_CAPTURE`) and regenerate `Port/generated/vu1`.
   Opening-cutscene leftovers, most visible first: the clipped ground-haze polygons with wrong edge alpha (VU1 side
   of the GL path, see Partially working; the brighter lower tower wall at ~7790; PCSX2's own stream renders correctly
   through our GS), the far mist billboards dropped by the GL clipper, and the slightly wider light shaft at the
   towers. VU0 macro add/sub do not get the PS2
   operand alignment (PCSX2's microVU does not either). The GPU backend's `SnapshotVram` should read render targets
   back so GPU recordings get correct checkpoints.
2. Compare the opening cutscene with a PCSX2 GS dump draw for draw (clouds, hawk, cliffs); re-check the
   menu against PCSX2 now that the solar flare is gone. The faint horizontal stripes on full-screen
   bilinear quads come from the rasterizer's float barycentrics under the EE's round-toward-zero MXCSR
   (see discoveries); fixing them is a deliberate output change (e.g. fixed-point GS interpolation checked
   against PCSX2) and needs new golden recordings.
3. VU0 registers other than R are per context (each thread/handler has its own copy); on the PS2 they are
   shared. Sharing them all needs thread switches only where the PS2 makes them (see the haze note).
4. Memory card on SIO2 ports 2/3.
5. Audio: SPU2 on the IOP side.
6. Sub-field timed waits in the scheduler; calibrate disc timing against PCSX2; IOP VBlank at the PAL rate.
7. argv, kernel object ID numbering.
