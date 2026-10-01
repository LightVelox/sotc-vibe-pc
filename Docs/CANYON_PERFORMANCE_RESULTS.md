# Saved lake/canyon results, 2026-10-01

Implemented, built, and validated on the user's lake/canyon state. No reliable overall gameplay FPS
or smoothness improvement is established by the preparation changes. The initial apparent gain
does not survive comparisons with similar background load. The optional blur switch removes an
identified camera-motion pass, but its FPS effect is also inconclusive. No interpolation, simulation
speed increase, physics change, or altered VU arithmetic was introduced.

## Build and private launch

The runnable executable is `build/port/bin/sotc.exe`, SHA-256
`93D5B9D0157A05DAB25BD3BB824EDE32DCC94C5F81F2276C73162F00981CCCAC`.
Rebuild with `build.bat`. The existing CMake configuration reuses the unchanged generated EE archive
at `build/performance-baseline-20261001/sotc_game_code.lib`; the changed host/runtime sources are rebuilt.
The generated EE and VU program sources were not edited.

From the repository directory, launch this scene with private cards and a separate F5/F9 slot:

```powershell
.\Tools\private_state_launch.ps1 -State .\build\canyon-20261001\canyon.state
```

The launcher copies the executable, INI, state and cards into `build/private-play`. Existing private
cards and INI choices persist. It loads `input.state` once and leaves `states/quick.state` separate.
Add `-DisableMotionBlur` to enable the optional switch for that launch. The launcher was exercised
in `build/canyon-20261001/play-test`: the game loaded field 104074, used that directory's card, and
closed normally. `run.bat` remains the ordinary launcher for the main installation.

The original executable and PDB are preserved in `build/canyon-20261001/baseline-bin`; the diagnostic
baseline is in `instrumented-baseline-bin`, and the preparation build before the fingerprint experiment
is in `pre-hash-fix-bin`. The original repository commits remain `11c62a1` and submodule `1808cc1`.
Changes are left in the working trees. Both trees were clean before this task.

## Fixture and camera constraint

The state is the user's `build/port/bin/states/quick.state`, copied privately to
`build/canyon-20261001/canyon.state`. Both retain SHA-256
`3ACA61445C3E00836FA19A8BCF22BBE34888B4A5D94C6C01F72739FBCC5CC74D`.
The original `build/port/bin/memcards/Mcd001.ps2` retains SHA-256
`4B3700109CCF7C994770A97B745D513A40C6FC4184167F87D0069E29F7AA1027`.
Every timed launch used another private state/card copy. No original quick-slot or card writes occurred.

Framebuffer captures show Wander on Agro above a lake, a stone bridge, and the canyon walls.
The shrine was not substituted. The initial view and four slow-circle extremes were inspected in
`identify` and `camera-calibration`. The command
`SOTC_CAMERA_TEST=104200:240:10:5` approaches the circle over 60 fields, performs five four-second
circles, and returns to center over 60 fields. It supplies mouse deltas through the existing native
camera path, with no player movement input. Yaw and pitch remain within approximately ±1.43 degrees.
The measured window is fields 104260 through 105460, approximately 20 seconds.

The inspected extremes and before/after images keep the same expensive view. Contact sheets are
`primary-views.jpg`, `full-views.jpg` and `blur-views.jpg` in the evidence directory. Window captures
after private reload and resize also show that canyon. Camera CSVs corroborate the entire scripted
path: the maximum component residual against the circle, allowing the previous camera update's field,
is below 0.009 degrees in all six final hashed comparisons. The returned center differs by roughly
0.007 degrees from the initial angles, from float accumulation. Target motion is ordinary horse/rider
idle motion; the input never moves the player. Visual sampling is not continuous recorded video.

## Measurements and acceptance

Hardware: Ryzen 5 5500, 12 logical processors, RTX 3060, NVIDIA OpenGL driver 616.64. The window
swaps near 75 Hz with VSync enabled. NTSC and both existing host-pacing switches remain enabled.
The previous direct presentation, compact commands, eight-chunk queue, VIF unpack and VU clipping
optimizations stay enabled in both sides. This task's baseline disables only its four preparation switches.

Primary comparisons omit hashing, profiling, GPU queries and camera CSVs. Screenshots are outside
the measured window. Baseline/new order alternates, including reversed pairs. There are four pairs
for the first three changes (`primary-*`), four for all preparation changes (`full-*`), and three on
the final executable (`final-*`). Separate hashed/camera-trace comparisons are `hash-*` and `hash2-*`.
Resource samples record system and game CPU time; no competing application was stopped or changed.
All 52 capture launches closed with exit 0.

The following are medians of three final alternating pairs. Percentiles are calculated per run and
then summarized by their median, rather than pooling intervals from different loads.

| Primary timing metric | Baseline | New |
|---|---:|---:|
| Emulated VBlanks/s | 59.936 | 59.947 |
| Actual scheduler updates/s | 52.201 | 51.856 |
| Scheduler rate range | 49.697–53.445 | 49.656–54.347 |
| Completed compositor sources observed/s | 59.161 | 58.690 |
| New compositor sources delivered/s | 59.111 | 58.690 |
| Host swaps/s | 74.974 | 74.973 |
| Repeated-source swaps per approximately 20 s | 316 | 320 |
| Update interval median / p95 / p99, ms | 17.184 / 32.866 / 35.927 | 17.095 / 32.725 / 39.345 |
| Source delivery interval median / p95 / p99, ms | 13.400 / 26.740 / 26.963 | 13.400 / 26.733 / 27.200 |
| Worst observed update gap across the three runs, ms | 237.656 | 218.259 |
| Update gaps above 50 ms across all three windows | 3 | 13 |
| Game-clock seconds per wall second | 0.99991 | 1.00001 |
| Other processes' busy CPU cores | 2.358 | 2.548 |

The final submission p95 medians are 0.850 / 0.434 ms, but this does not translate into a robust
update-rate or tail improvement. The earlier four-pair preparation comparison gives median rates
54.495 / 53.569 updates/s, with 2.204 / 2.461 other busy cores. Its update p99 medians are
34.987 / 35.105 ms. The first three-change comparison looked better, but its baseline had substantially
more background load. Similar-load samples overlap. No general FPS percentage gain is accepted.

Changed-image measurements below are a separate three-pair cohort, with substantially lighter load
in most windows. They must not be combined with the primary update rates as though recorded together.

| Fingerprinted diagnostic metric | Baseline | New |
|---|---:|---:|
| Actual scheduler updates/s in these runs | 59.345 | 58.640 |
| Changed images delivered/s | 56.742 | 56.693 |
| Changed-image rate range | 55.656–57.392 | 47.851–56.904 |
| Completed sources delivered/s | 59.839 | 59.839 |
| Host swaps/s | 75.023 | 75.024 |
| Repeated-image swaps per approximately 20 s | 366 | 366 |
| Changed-image interval median / p95 / p99, ms | 13.400 / 39.900 / 40.076 | 13.400 / 39.900 / 40.100 |
| Median of per-run maximum image gap, ms | 53.400 | 53.400 |
| Other processes' busy CPU cores | 0.775 | 1.002 |

The new third hashed run had 2.636 other busy cores and fell to 49.450 updates/s and 47.851 changed
images/s; the first two new runs had approximately 0.76–1.00 other cores and 58.64–59.09 updates/s.
The baseline hashed runs were all near 0.75–0.79 other cores. This is further reason not to claim a gain.
Across final primary, hashed and blur cohorts, game-clock progression is 0.99811–1.00010 seconds per
wall second. Idle animations continue; no timing, cycle-accounting or pacing policy was changed.

The 64-bit fingerprint covers the composited source pixels and dimensions, not physical scanout;
collisions remain possible. Completed source sequence span also counts published sources skipped by
the window through its last acquired completed fence. It is not an exact GPU completion timestamp trace.
Field-sampled update gaps can miss multiple updates inside a field. Camera CSVs provide direct camera
entry intervals in the separate diagnostic runs. Swap returns do not establish input-to-photon latency.

Early hash runs showed only roughly 41–46 changed images/s under higher background load. A diagnostic
experiment reduces the hash in an unmapped GPU buffer, then copies eight bytes to the coherent mapped
readback slot. `PS2X_FRAME_HASH_DEVICE=0` restores the old destination. A later quiet old-path repeat
also delivered 59.889 sources/s and 57.192 changed images/s, versus 59.839 and 56.693 for the new-path
quiet sample. A reliable fingerprint speedup is not established. The experiment is separate from gameplay
performance and has no effect when hashing is disabled. No sparse-pixel approximation was substituted.

## Causes found and implemented work

The evidence supports costly CPU game/graphics preparation and VU geometry, strongly affected by
background load, as the main sources of missed actual updates. Completed composition can remain
near 60/s while actual updates are near 50/s: many outputs repeat game content. A 75 Hz window then
quantizes fresh-image delivery into roughly 13.3, 26.7 and sometimes 40 ms gaps. Frequent swaps are
therefore not evidence of smooth 60 FPS gameplay.

Separate canyon stack samples include profiler overhead and intentional pacing waits. The VU sample
spends approximately 64.5% inclusive in VU execution and 25.8% waiting; three compiled blocks account
for approximately 30% self time. Ordinary logs show approximately 11–13 ms VU work per field.
EE work includes dispatch, game/model/graphics preparation, memory operations, IOP/audio and pacing.
The hottest generated VU segments already use SIMD and normalized register locals; their exact FMAC,
pipeline flags and register-pressure costs remain. Approximate arithmetic and timing shortcuts were rejected.

GPU-query profiling observes substantial raster and transfer/page-copy work, but query resolution itself
adds large driver waits. Its per-presentation figures are not whole-game frame times and cannot establish
the GPU's primary critical path. Shared presentation submission is short in ordinary runs; the evidence
does not identify the previous RAM presentation round trip as the current main problem. Transfer hazards,
copy-on-write, ordering, and the bounded queue were retained.

Implemented changes, all independently reversible by setting the named switch to `0` and restarting:

| Change | Purpose | Acceptance |
|---|---|---|
| `SOTC_FAST_OBSERVERS` | Reuse an immutable observer list instead of copying vectors/functions at each guest hook | Correctness passes; FPS/smoothness gain inconclusive |
| `PS2X_EE_SINGLE_LOOKUP` | Resolve a valid guest call target once instead of checking and looking it up again | Missing-target/unwind/runtime tests pass; FPS gain inconclusive |
| `PS2X_GS_TEXTURE_PAGE_CACHE` | Reuse an identical texture address footprint across primitives | Live pixel/hazard handling retained; GPU/VRAM equivalence passes; FPS gain inconclusive |
| `PS2X_GS_FAST_TRIANGLE_SETUP` | Replace expensive library rounding/bounds calls in valid bounded GS coordinates | Rounding, edges and coverage match; FPS gain inconclusive |

These preparation switches default to `1`. Their reduced work is supported by the profiles and code
paths, but no change is accepted as a reliably measured increase in overall gameplay FPS or smoothness.
The camera trace follows the requested circle closely, including quiet runs with near-stable source
delivery. It supplies no evidence of an additional native-mouse angle-response defect. Physical mouse
feel and the original right-stick easing path were not directly assessed. Camera behavior was not changed.

## Motion blur

The separate framebuffer-feedback level at `0x128F520` is zero throughout the diagnostic scene;
`feedbackBlur` and `feedbackBlur2` were not called. The camera-blur enable at `0x128A134` is one.
The game's `cameraBlur` at `0x1180800` computes camera-motion history and conditionally calls
`screenBlur` at `0x117FDF8`, returning to `0x1180F90`. That draw does not fire in the slow circle,
but does fire in `104200:60:20:20`, a one-second circle within approximately ±2.86 degrees.
This identifies an actual motion-dependent blur pass, independently of fog, bloom and low-FPS judder.
It does not establish broader temporal framebuffer accumulation in this scene.

`SOTC_DISABLE_MOTION_BLUR=0` preserves original behavior and is the default. `1` bypasses only that
specific `screenBlur` call. The original motion-history and intensity calculations continue, and the
guest enable flags are neither overwritten nor persisted differently. Other callers, feedback passes,
ordinary blending, bloom, fog and shadows remain. The setting is added to missing INI files as `0`.

Three alternating fast-circle pairs measure median 58.895 updates/s with blur and 58.998 without.
Update p99 is 30.268 / 32.622 ms; source delivery remains approximately 59.9/s. This does not establish
a blur-disable FPS or smoothness gain. `blur-visual-0` and `blur-visual-1` show the canyon, characters,
bright-wall bloom, haze and bridge correctly with the same bounded input. The subtle effect and different
capture phases do not establish a strong subjective clarity benefit from these stills. The switch is an
optional clarity preference, not a cure for missed updates or display persistence.

## Validation, limits and rollback

- Runtime suite: **499/499 passed**, including a coordinate oracle checking 524,292 signed/halfway/
  neighboring rounding cases plus 16,384 random triangle setups and the existing dispatch/unwind tests.
  Log: `build/canyon-20261001/runtime-tests-final.txt`. The initial invocation used the wrong working
  directory for a pre-existing source-inspection test; rerunning from `External/PS2Recomp` resolves it.
- Fresh canyon VU trace: **3,192 executions over three fields, zero interpreter mismatches**.
  Log: `vu-canyon-verify.txt`, fixture: `profile-ee/trace.bin` and its captured microprogram images.
- Original versus optimized GPU preparation: **zero pixel and VRAM differences** at the two compared
  canyon checkpoints and every compared checkpoint in menu language, menu Hz, loading, logo and
  cutscene recordings. Logs: `equivalence-*-final.txt`. Use `--compare-backends gputhreadoriginal gpu`:
  each GPU backend needs its own owning context/thread. The initial same-thread two-GPU comparison
  was invalid and is excluded. The replay tool now returns failure for detected differences.
- State/window: private F5 save, F9 reload, F11 fullscreen both directions, resize to a 1264×681 client,
  verified minimization, restore and normal close pass in `window-minimize-final`. Its post-restore
  `field-105650.png` shows the correct canyon. The earlier PostMessage minimize test did not confirm
  minimization; the helper now uses `ShowWindow`, and the final run reports minimized true then false.
  PrintWindow captures are blank for this OpenGL window and are excluded from visual evidence.
- Stable gameplay/audio intervals report **zero missing frames, underruns and dropped frames**;
  state-load queue resets are separate. Physical listening, controller vibration and audio fidelity are
  not established by queue health. Existing SPU2 fidelity limitations remain.
- Original state/card hashes were checked again after all tests. Controls, guest timing, save formats,
  animation/physics code and audio synthesis were not modified. This is short scene validation, not a
  complete playthrough, ordinary card-save test, physical-display measurement or end-to-end input test.

Remaining costs are generated EE game/graphics work, VU geometry/flags/spills, GS triangle/binning work,
GPU raster/transfer/page-copy work and host/background scheduling. Moving geometry to the GPU would
require preserving CPU-visible VU memory, flags, stalls and XGKICK order; it was not attempted as an
approximation. The build does not guarantee 60 genuinely new gameplay images/s.

For behavior rollback, set the four preparation switches to `0`, `SOTC_DISABLE_MOTION_BLUR=0`, and
remove `SOTC_CAMERA_TEST` / `SOTC_CAMERA_TRACE`; restart the game. Set `PS2X_FRAME_HASH_DEVICE=0`
to reverse the diagnostic destination experiment, or leave `PS2X_FRAME_HASH=0` for normal play.
Each change can be disabled separately. Full binary rollback, with the game closed:

```powershell
Copy-Item .\build\canyon-20261001\baseline-bin\sotc.exe .\build\port\bin\sotc.exe
Copy-Item .\build\canyon-20261001\baseline-bin\sotc.pdb .\build\port\bin\sotc.pdb
```

No card/state restoration is required: their originals were never changed. Private test folders are
independent of the original installation. Aggregate numbers and individual reports/settings/frames/
timelines/resources are retained in `build/canyon-20261001/results.json` and its named capture folders.
