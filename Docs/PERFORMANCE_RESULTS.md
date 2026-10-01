# Performance results, 2026-10-01

The saved lake/canyon investigation is recorded separately in
[CANYON_PERFORMANCE_RESULTS.md](CANYON_PERFORMANCE_RESULTS.md). It uses the user's private
quick-state copy and a bounded camera circle, and does not reuse the shrine as its benchmark.
The results below belong to the earlier committed work and remain historical evidence.

The user authorized validation after the initial build. Shared GPU presentation consistently reduced
host submission cost. Overall gameplay FPS and frame-delivery gains remain inconclusive: the game
still runs around the low-to-mid 30s in these shrine samples, and other processes substantially affect
the comparisons. This is not a 60 FPS gameplay result.

## Runnable build and configuration

Use `run.bat`, which launches `build/port/bin/sotc.exe`. The executable and VU benchmark were rebuilt
using the unchanged generated EE library and the new runtime/VU code. The original executable, PDB,
and EE library are preserved in `build/performance-baseline-20261001`.

The installed INI retains `SOTC_VSYNC=1`, and enables direct presentation, compact GS commands,
an 8-chunk queue limit, SIMD VIF unpack, and SIMD VU clipping. Each is independently reversible;
see [PERFORMANCE.md](PERFORMANCE.md). Cached shader binaries for the tested GPU were copied into
`build/port/bin/game_data/gs_shaders`. Driver/source changes invalidate them automatically.

## Method and limitations

Captures used private executable/settings/card copies, the same shrine state saved at field 3001,
and scripted left-stick movement or right-stick camera rotation. The primary window is fields
3400-4600, about 20 seconds; three alternating baseline/all pairs were run for each scene. The shorter
follow-up uses fields 3400-4300, about 15 seconds, with two alternating pairs. Baseline means this same
build with the five experiments disabled, retaining the earlier VSync and asynchronous readback work.
Shader cache state is held warm for timing. Each run must close normally.

The tested GPU reports NVIDIA GeForce RTX 3060, OpenGL 4.6, driver 616.64. VSync-on host swaps were
about 75/s. Timing CSVs measure API completion, not physical monitor scanout. Scheduler updates are
the delta of guest counter `0x1DC9EC`; the game clock uses the 64-bit timer at `0x1F8A98/0x1F8A9C`
divided by 147,456,000 ticks/s. Fields, game updates, compositor outputs, changed images, and host
swaps are deliberately separate measurements.

Background load remained variable, even after the user paused other work. In the short follow-up,
other processes averaged 1.3-4.8 busy CPU cores during VSync-on shrine windows, and 3.6-5.3 during
the VSync-off comparison; the game averaged roughly two cores. A prior Valus repeat changed from
34.3 to 27.4 updates/s as other-process load changed from 3.4 to 6.2 cores. Do not attribute all observed
FPS differences to these code changes. No unrelated processes were stopped by the capture tools.

Timed captures precede the final minimized-window handling and VU switch-helper inline change.
The ordinary visible presentation path is unchanged by the window fix. Final VU verification and
smoke evidence are recorded separately below; no extra gameplay FPS gain is attributed to inlining.

## Primary shrine comparisons

Entries are medians of three per-run rates/percentiles. These are descriptive observations, not a
statistically established overall speedup.

| Scene | Baseline updates/s | All updates/s | Submission p95, baseline/all | Game update gap p99, baseline/all |
|---|---:|---:|---:|---:|
| Camera rotation | 33.426 | 34.017 | 1.852 / 0.321 ms | 50.717 / 49.389 ms |
| Movement | 33.011 | 32.910 | 2.085 / 0.319 ms | 52.084 / 49.999 ms |

The submission p95 reduction is about 83-85%. It removes a concrete RAM round trip and window-thread
wait/copy/upload work. It does not establish an equal improvement in total frame time or physical
input latency. Host swaps remain about 75/s, NTSC fields about 59.9/s, and the game timer progresses
at approximately real time (about 0.9996-1.0002 seconds per wall second in these windows).

Raw evidence: `build/performance/primary-vsync/results.json`, with each run's `frames.csv`,
`timeline.txt`, `stdout.txt`, and report. The single-pair `isolate-vsync/results.json` tests presentation,
compact queue, queue bound, VIF unpack, and VU clip independently. Its run variance is too large to
rank the small CPU experiments reliably; presentation submission reduction repeats across it.

## Short follow-up and scene samples

| Scene/configuration | Baseline updates/s | All updates/s | Submission p95, baseline/all |
|---|---:|---:|---:|
| Camera, VSync on, median of two | 31.539 | 34.536 | 2.325 / 0.317 ms |
| Movement, VSync on, median of two | 32.134 | 32.833 | 1.880 / 0.320 ms |
| Valus arena, VSync on, one pair | 36.034 | 36.020 | 2.093 / 0.383 ms |
| Camera, VSync off, one pair | 27.710 | 32.173 | 2.237 / 0.284 ms |

The shorter camera run improved, movement changed little, and Valus did not improve. One camera
baseline had 4.8 other busy cores versus 2.0 in its all counterpart. The VSync-off baseline also had
more background load. These samples cannot justify a general percentage gain or changing the VSync
default. Camera update p99 improves in that follow-up, but movement and Valus tails do not consistently
improve. The frame-delivery problem is therefore reduced at presentation and remains in gameplay.

Saved-state samples named `colossus01`, `colossus02`, and `colossus13` also completed normally.
Internal framebuffer captures show the arena/shrine environment, Wander, Agro, bloom, fog, and
shadows. These short camera samples are not a validation of complete fights or every heavy scene.
The first broad arena batch recorded 44.1/40.3, 32.7/35.6, and 32.7/34.0 baseline/all updates/s,
respectively; Valus repeats ranged much more widely with background load. They are not accepted
as measured gains or proof of a regression.

Raw evidence: `build/performance/quiet-vsync/results.json`, `quiet-valus/results.json`,
`quiet-vsync-off/results.json`, and `heavy-vsync/results.json`. CPU measurements are in
`resources.csv` and each report's `cpu_cores_busy`.

## Distinct images versus fields and swaps

Separate hashed runs add work and are used for cadence diagnosis, not the primary FPS comparison.
The all-enabled camera sample records 59.94 fields/s, 36.02 scheduler updates/s, 57.74 compositor
sources/s, 34.57 changed images/s, and 75.02 host swaps/s. There were 810 repeated-image swaps in
about 20 seconds. Movement records 34.96 updates/s and 34.52 changed images/s.

Camera changed-image gaps have median 26.7 ms, p95 40.1 ms, and p99 53.4 ms. The monitor cadence
quantizes those gaps. A newly completed compositor texture often contains an unchanged game image;
neither its sequence nor the field counter proves 60 distinct gameplay frames. Fingerprints are
64-bit reductions and can collide. They do not prove full-frame pixel equality.

Raw evidence: `build/performance/hash-vsync/results.json` and per-run reports.

## Correctness evidence

- Runtime suite: **498/498 passed**, including CPU/threaded GS goldens and a new independent VIF
  unpack oracle covering 648 combinations of width, components, signedness, row mode, count, and
  skip cycles. It checks all 16 KB of VU memory, wraparound, preserved lanes, and mode-2 row updates.
  Log: `build/performance-tests-3.txt`.
- Compiled VU versus interpreter: **8,046 executions over four shrine fields, zero mismatches**,
  using captured `2048debd5c78af0a` and `d18c8dfaae098293` images. Log:
  `build/performance/vu-verify.txt`; fixture: `build/performance/warm-cache/trace.bin`.
  The rebuilt final code repeats this with zero mismatches in `vu-verify-final.txt`.
- Ordinary GPU versus compact threaded GPU: **zero pixel and VRAM differences at every compared
  checkpoint** for menu language, menu Hz, loading, logo, and cutscene recordings. Logs:
  `build/performance/gpu-queue-*.txt`. This proves queue equivalence for those recordings, not
  GPU/CPU rasterizer equivalence. GPU versus CPU goldens retain the historical raster differences
  documented in PROGRESS.md; those were not hidden or changed by this task.
- Save-state/window exercise: F5 saves, F9 restores, F11 enters and leaves borderless mode, resizing
  works, minimize/restore resumes presentation, and the process closes with exit 0. A framebuffer
  capture after restoration is `build/performance/window-validation-4/field-4500.png`.
  The first attempts exposed an automated-window-selection problem after minimization; the tool
  now retains the original HWND. Minimized runtime polling also permits close events and skips
  unnecessary hidden-window drawing.
  The final rebuilt executable repeats this exercise with exit 0 in `build/performance/final-smoke`;
  its post-restore image is `field-4500.png` and executable SHA-256 is in `settings.json`.
- Shader cache fallback: a private truncated cache entry was rebuilt, and menu GPU/threaded replay
  still had zero pixel/VRAM differences. Log: `build/performance/cache-fallback-validation.txt`.
- The sampled stable shrine audio reports contain zero missing frames, underruns, or dropped frames.
  Startup/state-load resets are separate. Physical listening was not performed.

The original memory cards and quick-state slot were not used for writes. Ordinary in-game card save/load,
mouse feel, controller vibration, audio fidelity/listening, physical scanout, and input-to-photon latency
remain outside this automated validation. Existing audio fidelity gaps remain documented in AUDIO.md.

## Startup and VU microbenchmarks

An uncached launch reached the host-display log at 100.538 s. Warm launches typically reached it
around 0.55-0.61 s on the same GPU, although startup varies (one follow-up baseline was 1.24 s).
This is a warm-start improvement from reusing linked shaders, not a gameplay FPS result. A first
launch with a different driver or shader source still compiles. Evidence: `warm-cache/stdout.txt`
and warm capture logs with nine shader binary hits.

Before the final switch-helper inline change, three alternating 100-repeat trace runs measured scalar
clip at 10.970/12.040/10.527 million CPU thread cycles per field and SIMD at
10.527/10.765/10.781. The spread overlaps, so this does not establish a robust clip speedup.
Logs: `build/performance/vu-clip-{0,1}-{1,2,3}.txt`.

After profiling showed the switch helper in hot VU blocks, it was made force-inline and all generated
VU code was rebuilt. Three alternating 100-repeat comparisons of the prior and final benchmark
executables give 10.241/10.105/11.242 million CPU thread cycles per field before and
10.328/10.305/10.212 after. These overlap; no further measured speedup is claimed. Logs:
`build/performance/vu-inline-{before,after}-{1,2,3}.txt`.

## Remaining bottlenecks

Current separate 10-second stack-sampling captures are `current-profile-ee` and `current-profile-vu`.
They include waits and profiler overhead; percentages are not interchangeable with useful CPU time.
The VU sample spends about 53.6% inclusive in `VU1Interpreter::run`, with the hottest compiled block
at 16.2% self, VIF fast unpack at about 4.1% inclusive, and XGKICK handling about 3.7% inclusive.
EE work includes game model/graphics preparation, dispatch, memory access, collision, and scheduler/
IOP/audio work. Copies and allocations are present but do not individually dominate these captures.

Exact VU FMAC behavior, pipeline/flag handling, generated-code register spills, and game graphics
preparation still cost CPU time. Moving all VU geometry onto the GPU would require preserving its
CPU-visible memory, flags, stalls, and XGKICK ordering; the current GPU renderer does not provide
that architecture. Approximate arithmetic or removing scheduler/DVD waits would risk the requested
game behavior. No ISO extraction or artificial game-speed increase was used.
