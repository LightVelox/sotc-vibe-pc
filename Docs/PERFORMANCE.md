# Performance implementation, 2026-10-01

This build adds shared GPU presentation, compact GS command submission, bounded command backlog,
SIMD VIF unpacking, SIMD clipping in compiled VU1 code, and a driver-specific shader binary cache.
The user subsequently authorized validation. Runtime tests, VU interpreter comparisons, GPU queue
replays, scripted gameplay, and window/save-state checks were run. See
[PERFORMANCE_RESULTS.md](PERFORMANCE_RESULTS.md) for measured results and their limits. Presentation
submission cost falls substantially; a reliable overall gameplay FPS gain has not been established.

## What changed

The Windows host GL context is created before the GS backend so the worker's OpenGL 4.6 context can
join its share group. A three-texture ring receives the existing CRTC compute output through an
OpenGL pixel-unpack buffer. This is a GPU buffer-to-texture transfer; the full image no longer travels
through mapped CPU memory, intermediate pixel vectors, or `UpdateTexture` on the window thread.
The compositor shader, CRTC circuit blending, output dimensions, bilinear filtering, aspect-ratio
handling, and original GS rendering stay in place.

The window queues presentation without draining the GS worker, polls fences at each host refresh,
and selects the newest completed texture. It retains that texture while the worker is busy. A separate
fence protects the window's last use before the producer reuses a texture; pending slots are not
overwritten. Repeated requests without VRAM writes or CRTC changes skip compositing. A full ring
also skips compositing. GS commands still execute in order. Failed WGL context sharing falls back to
the existing RAM path. GS backend destruction now runs on the worker that owns its GL context.

The threaded GS queue stores a full draw state when its bytes change and compact vertex records
for the following primitives. Its consumer reconstructs `GSPrimitiveBatch` before calling the backend.
MXCSR, CLUT operations, transfers, synchronization, and state import/export retain their ordered
queue positions. The queue's default outstanding-chunk limit is 8 instead of 256, reducing the
amount of CPU work permitted to accumulate ahead of rendering. This limit needs tuning against CPU/GPU
overlap on the user's machine. The GPU upload ring also copies only the actual payload size before
padding its minimum allocation; it previously read padded bytes beyond short payloads.

The VIF fast unpacker uses SSE integer widening, lane preservation/broadcast, and wrapping row
addition for unmasked vectors. Masked transfers, fill cycles, packed V4-5 data, memory wraparound,
and source byte counts keep their existing handling. Compiled VU1 clipping now compares all lanes
with SIMD integer operations and produces the same intended clip-bit ordering and denormal limit.
Neither change introduces approximate floating-point math or alters charged VU/EE cycles. All
substantial paths have switches to restore the previous implementation.

The shader cache stores linked program binaries with the exact GLSL source, driver identity, and
binary checksum. Missing, truncated, incompatible, or rejected entries fall back to source compilation.
Uniform locations are obtained again after loading. This removes repeated shader compilation on warm
starts without changing shader source. `PS2X_GS_SHADER_CACHE=0` disables it;
`PS2X_GS_SHADER_CACHE_DIR` overrides the default `<exe dir>/game_data/gs_shaders` directory.

The minimized window continues polling input and close/restore events at 120 Hz while skipping host
drawing and compositing. The game thread retains its timing. This also avoids doing presentation work
for a hidden window. The window exercise tool retains the same HWND across minimize/restore, because
selecting the largest current client area can otherwise target another window in the process.

The existing generated EE optimization flags (`/d2OptimizeHugeFunctions`, `/Ob2`, `/arch:AVX2`,
`/fp:precise`) and runtime LTCG are retained. Historical compiler experiments, flag specialization,
wider exact VU arithmetic, and alternate block layouts already failed to establish useful gains;
they were not repeated. Arbitrary native replacements of game logic and FMAC approximations would
need stronger correctness evidence before changing physics, animation, or geometry.

ISO streaming and modeled DVD delays retain their current implementations. Historical evidence
points at CPU geometry/game-command work, not host ISO reads, and loader interleaving depends on
the modeled delay. Archive extraction was not pursued.

## Switches and rollback

These settings default to the enabled experimental paths in this port. Existing explicit INI values
and environment overrides are preserved. Missing values are applied and added on launch.

| Setting | New default | Previous path |
|---|---:|---:|
| `PS2X_GS_DIRECT_PRESENT` | 1 | 0 |
| `PS2X_GS_COMPACT_QUEUE` | 1 | 0 |
| `PS2X_GS_QUEUE_CHUNKS` | 8 | 256 |
| `PS2X_VIF_SIMD_UNPACK` | 1 | 0 |
| `PS2X_VU1_SIMD_CLIP` | 1 | 0 |
| `PS2X_GS_SHADER_CACHE` | 1 | 0 |

Edit `build/port/bin/sotc.ini` while the game is closed, then use `run.bat`. Disabling direct
presentation retains the previous nonblocking readback work from the existing working tree.
`SOTC_VSYNC=1` still uses monitor synchronization with no extra software limiter; `0` still uses the
120 Hz host polling limit. Compare both on the same monitor and resolution.

The pre-task executable and its PDB are preserved in `build/performance-baseline-20261001/`.
For a full executable rollback, close the game and copy its `sotc.exe` and `sotc.pdb` back to
`build/port/bin/`, then disable the new switches. Saves and the original card directory were not
edited by this task.

## Build

`build/port/bin/sotc.exe` is built with the new host/runtime/VU code. The VU benchmark is also rebuilt
for the user to run. The unchanged generated EE archive is preserved as
`build/performance-baseline-20261001/sotc_game_code.lib` and selected by the explicit CMake cache
option `SOTC_GAME_CODE_LIBRARY`. This avoids recompiling 9,216 unchanged EE functions after internal
GS header changes. The generated EE code does not construct or access `GSThreadedBackend` internals;
the runtime interfaces and layouts that its existing binary uses were not changed.

Use the usual `build.bat` for these host changes. Before modifying generated EE code, its inlined
runtime helpers, the EE-visible runtime ABI, or regenerating from the disc, clear the cached-library
option and do a source build:

```powershell
.\configure.bat '-DSOTC_GAME_CODE_LIBRARY='
.\build.bat
```

The source build retains the full huge-function optimizations; historical builds took around an
hour. The imported-library option defaults empty in clean configurations.

## Reproducing validation: shrine and camera

Run from the repository root in PowerShell with Python. Each capture copies the executable and
settings into a new directory and clones cards for saved-state runs. Cold-boot shrine captures
disable cards to reproduce the documented no-card route. They leave the original executable,
settings, quick state and cards alone. Keep the game visible, avoid concurrent instances and
background compilation, and confirm the scripted route actually reaches gameplay in the measured
window. If a loading screen or menu is still visible, discard that window and adjust the script.

The saved shrine fixture from this session is `build/performance/shrine-smoke-3/capture.state`.
The suite alternates variant order and measures the same field window for each run. Use fresh output
directories and keep hash/profiler/screenshot work outside primary timing windows:

```powershell
python Tools/performance_suite.py shrine --state build/performance/shrine-smoke-3/capture.state --out build/performance/recheck-shrine --repeats 3
python Tools/performance_suite.py isolate --state build/performance/shrine-smoke-3/capture.state --out build/performance/recheck-switches --repeats 2
python Tools/performance_suite.py hash --state build/performance/shrine-smoke-3/capture.state --out build/performance/recheck-images --repeats 1
python Tools/performance_suite.py heavy --state build/saved_states --out build/performance/recheck-arenas --repeats 2
```

`--window-fields 900` shortens the measured window. `--only rotate` or `--only colossus01` selects
a scene; `--vsync 0` tests the 120 Hz host poll path. Inspect `results.json` and the background CPU
load in each report before interpreting FPS changes. The suite rejects captures that fail to close
normally. The harness copies symbols only with `--copy-symbols`; normal profiling can use the
original PDB in `build/port/bin`, avoiding hundreds of megabytes per capture.

To reproduce the separate window/save-state exercise:

```powershell
python Tools/performance_capture.py all --scene manual --state build/performance/shrine-smoke-3/capture.state --pad-script '3100v:rright:2400' --exercise-window --seconds 39 --screenshot-fields 3400,3900,4500 --out build/performance/recheck-window
```

Start with movement and camera rotation, alternating baseline/new/baseline/new at least three times
per configuration. Choose a fresh output path for every run:

```powershell
python Tools/performance_capture.py baseline --scene shrine --out build/performance/shrine-base-1
python Tools/performance_capture.py all --scene shrine --out build/performance/shrine-new-1
python Tools/performance_capture.py baseline --scene rotate --out build/performance/rotate-base-1
python Tools/performance_capture.py all --scene rotate --out build/performance/rotate-new-1
python Tools/frame_report.py build/performance/shrine-base-1 --first-field 3000 --last-field 3900
python Tools/frame_report.py build/performance/shrine-new-1 --first-field 3000 --last-field 3900
```

The shrine script uses the historical `2300v:start` intro skip, then holds `ldown` at fields
2700-4199. The rotation script holds `rright` over the same interval. Scripted stick input provides
a repeatable camera comparison; it does not validate native mouse feel. Also manually load the same
shrine save, run down the stairs, rotate the mouse through the same sweep, reverse direction abruptly,
and compare controller and mouse responsiveness. Repeat with `--vsync 0` and normal unmuted audio.

Use the individual variants `presentation`, `compact`, `queue`, `unpack`, and `clip` to isolate
changes. Each enables only that experiment; `all` enables them together. `baseline` uses the current
executable with all five changes disabled, so the compiler and previous VSync/readback changes are
held constant. For profiles, add `--profile-thread GameThread --profile-window 70:15`, then repeat
for `VUThread`, `GIFThread`, and `GSThread` separately. Sampling adds overhead, so keep profiled runs
separate from primary timing captures.

Do a second pair with `--hash-images` to count changed images and repeated swaps:

```powershell
python Tools/performance_capture.py baseline --scene rotate --hash-images --out build/performance/hash-base-1
python Tools/performance_capture.py all --scene rotate --hash-images --out build/performance/hash-new-1
python Tools/frame_report.py build/performance/hash-new-1 --first-field 3000 --last-field 3900
```

Hashing adds work: GPU reduction plus an eight-byte completion read in the shared path, CPU hashing
of existing pixels in the RAM path. Use hash-free captures to judge performance and hashed captures
to distinguish repeated content. A 64-bit fingerprint can collide and is not an exact pixel comparison.

## Counters and acceptance

| Metric | What it establishes |
|---|---|
| Emulated VBlank rate | NTSC/PAL field clock progression |
| `0x1DC9EC` scheduler update counter | Actual game frame-loop cadence, sampled separately from fields |
| `0x1F8A98`/`0x1F8A9C` timer accumulator | Game clock progression at 147,456,000 BUSCLK ticks per second |
| `drawn/s` | Presentation intervals with primitive submissions; a proxy, not a distinct-image count |
| Completed source sequence | Compositor outputs delivered to window draws |
| Image fingerprint changes | Changed presented content versus repeated images, including UI changes |
| Host swap calls | Submitted window refreshes; does not prove physical scanout |
| Changed-image and update interval tails | Stalls hidden by average field/s or average FPS |

Compare scheduler updates/s, changed images/s, median/p95/p99/max update and image intervals, repeat
counts, submission cost, and audio underruns. Check `game_clock_seconds_per_wall_second` remains
near 1.0 over a stable gameplay window. A 60 Hz field rate alone is not an acceptance criterion.
The timeline observes RAM at field cadence, so it cannot resolve multiple updates inside one field.
For end-to-end input delay, record the same physical input and screen response with a high-speed
camera; the CSV measures host calls, not input-to-photon latency.

## Heavy scenes and correctness

Let the attract intro play using `--scene intro --seconds 210`; choose comparable windows from the
historical cloud/canyon/tower/passage shots only after confirming what is on screen. If using stored
states, capture the same `build/saved_states/colossusNN_fight.state` with `--scene manual --state <path>`
for baseline and each experiment. Use identical input and camera framing in Valus, Quadratus, and
a representative later colossus. Avoid comparing scene phases that start at different wall times.

For compiled VU clip verification, run the rebuilt benchmark against available trace fixtures with
`--verify`, or launch gameplay with `PS2X_VU1_VERIFY=1` and compare compiled execution with the
interpreter. The trace benchmark covers VU clipping, not the live VIF unpacker or shared presentation.
To capture a fresh shrine trace and check it with the rebuilt benchmark:

```powershell
New-Item -ItemType Directory -Force build/performance/vu-check | Out-Null
$env:PS2X_VU1_TRACE="$PWD/build/performance/vu-check/trace.bin:3000:4"
python Tools/performance_capture.py all --scene shrine --out build/performance/vu-check-run
Remove-Item Env:PS2X_VU1_TRACE
.\build\port\bin\sotc_vu1_bench.exe build/performance/vu-check/trace.bin build/performance/vu-check --verify
```

For live correctness checking, set `$env:PS2X_VU1_VERIFY='1'`, run `.\run.bat`, exercise the same scenes,
then remove that environment setting. This runs each VU program against its interpreter and deliberately
adds substantial work; keep it separate from performance measurements.
For VIF unpacking and compact submission, rebuild and run `ps2x_tests` with the recorded GS goldens
and use deterministic full-game golden captures with `PS2X_MTVU=0`. Direct presentation needs real
window comparisons and is not exercised by ordinary GPU replay without the host share context.

Build the runtime test target in the existing separate runtime build before running it:

```powershell
.\Tools\win\vsenv.cmd cmake --build build/rt --target ps2x_tests
```

The executable path can vary with that build's CMake configuration. See `Docs/TESTING.md` and
`Docs/PROGRESS.md` for golden recordings and VU trace command formats. Do not treat a replay-only
pass as acceptance for live OpenGL sharing.

Visually compare menus, loading, bloom, shadows, clipping, sprites, letterboxing, resize, fullscreen,
and PAL/NTSC changes. Run the original controls, listen for gaps/crackles, and exercise F5/F9 and
ordinary save/load using private card copies. Toggle fullscreen and resize while rotating the camera;
minimize, restore, and switch focus. Confirm logs show `[gs-present] shared GPU textures active` when
enabled, or a fallback when unavailable. If any experiment regresses output or frame delivery, disable
that switch and retain the other independently accepted changes.

## Saved lake/canyon procedure

The private fixture is `build/canyon-20261001/canyon.state`, copied from `build/port/bin/states/quick.state`.
The original quick slot and cards are preserved. For ordinary gameplay with private saves/cards:

```powershell
.\Tools\private_state_launch.ps1 -State .\build\canyon-20261001\canyon.state
```

Add `-DisableMotionBlur` for the optional clarity comparison. Existing private cards and INI choices
persist in `build/private-play`. The input fixture is separate from that directory's F5/F9 slot.

For a fresh 20-second measured circle, use:

```powershell
python Tools/performance_capture.py all --scene manual --state build/canyon-20261001/canyon.state --camera-test 104200:240:10:5 --out build/canyon-repeat --seconds 31 --end-field 105560 --screenshot-fields 104190,105520
python Tools/frame_report.py build/canyon-repeat --first-field 104260 --last-field 105460
```

`all` retains the previous committed optimizations. For this task's baseline add four settings:
`--setting SOTC_FAST_OBSERVERS=0 --setting PS2X_EE_SINGLE_LOOKUP=0
--setting PS2X_GS_TEXTURE_PAGE_CACHE=0 --setting PS2X_GS_FAST_TRIANGLE_SETUP=0`.
Do not use the harness's older `baseline` variant: that disables the previous committed changes too.
Alternate the runs. Primary runs omit hashing, sampling, camera CSV, GPU queries, and screenshots
inside the measured window. Separate `--hash-images --camera-trace` runs diagnose changed-image and
camera cadence. `PS2X_FRAME_HASH_DEVICE=0` restores the earlier CPU-mapped hash-atomic path;
the new default reduces in GPU memory and copies eight bytes after its barrier. Neither hash mode
affects ordinary gameplay when `PS2X_FRAME_HASH=0`.

The slow sequence stays within approximately ±1.43 degrees; `104200:60:20:20` stays within
approximately ±2.86 degrees and activates the original camera-blur effect. Confirm each source state
and the sequence's extremes visually before reusing these numbers for another state. No movement
stick is sent. Do not load another state during a measured circle; start each run from the fixture.
See [CANYON_PERFORMANCE_RESULTS.md](CANYON_PERFORMANCE_RESULTS.md) for current findings and limits.

## Remaining limits

Historical profiles still identify generated EE game/graphics preparation and VU geometry execution
as expensive. Exact FMAC safety/flag handling, CPU VU microprogram execution, texture-page hazards,
and GPU raster work remain. This task does not move the geometry programs to the GPU or guarantee
60 actual game updates or distinct images per second. Current profiles and comparisons are recorded
in [PERFORMANCE_RESULTS.md](PERFORMANCE_RESULTS.md); historical field-rate improvements are not
current results. Physical input-to-photon timing, subjective mouse feel, audio listening, and ordinary
in-game memory-card save/load were not validated by this automated session.
