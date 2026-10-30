# Native renderer

Plan for replacing GS emulation with native GPU rendering. Today every renderer (CPU, GPU compute and GPU
hardware raster) emulates the GS at the level of its 4 MB of swizzled VRAM at 512×448. A native renderer instead
draws the game's primitives into ordinary GPU render targets and textures. That allows higher internal
resolutions, removes the dependency on fragment-shader interlock (AMD on Linux uses the slow compute path today),
presents frames without a RAM round trip, and is the groundwork for porting the renderer to other GPUs and
consoles.

This document records what the game draws, which GS features it relies on, the design, and the milestones. The
findings come from two GS recordings (riding through the open field, and the opening cutscene), analysed with
`Tools/gs_frame_census.py`, and from the game's symbol names in `Port/generated/`.

## What a frame looks like

The game runs at half the field rate, so a frame spans two fields and ends with a copy into the display buffer.
Riding frame, 28,351 primitives (the cutscene frame has the same structure with 57,384):

| # | Target (FBP, size) | Draws | What it is |
|---|---|---|---|
| 1 | `e0` 512×448, depth `70` | 2,342 | First scene pass covering the screen (background, distant terrain) |
| 2 | `150` 256×256 | 8 | Downsampled copy of `e0` |
| 3 | `e0` | 22,543 | Main scene: terrain, Wanda, Agro, plus fog sprites that read `150` |
| 4 | `178` 512×448 | 2,424 | Shadow volumes: untextured, blended strips (`shadowDisp`) |
| 5 | `e0` | 8 | Shadow composite: sprites reading `178` (`shadowDraw`) |
| 6 | `e0`→`150`→`170`→`150`→`e0` | 56 | Down/upsample chain added back to the scene (glow) |
| 7 | `150` 512×448 | 200 | Small textured object drawn into a cleared scratch buffer |
| 8 | `150`→`1c0`→`1e0`→`1e8` and back to `e0` | 42 | Blur pyramid 256→128→64 (likely `depthOfField` / `feedbackBlur`) |
| 9 | `150` 256×256, depth `170` | 187 | Low-resolution second scene render |
| 10 | `170` | 128 | 128 offset copies of #9: radial blur for light shafts (`solarFlare`) |
| 11 | `e0` | 13 | Sun glare in the upper right |
| 12 | `1c0`…`1fe` | ~70 | Second blur with ±3 px taps |
| 13 | `e0` | 262 | Full-screen tint, then overlays |
| 14 | `0` 512×448, 24-bit | 1 | Copy of `e0` into the display buffer |

Rows 6–12 are interpretations from the pass shapes and the effect function names; they are confirmed by
toggling the effect functions in milestone 2. Each frame has about 2,200–2,400 runs of identical draw state,
which is the number of GPU draw calls a native renderer needs.

## GS features the game uses

Counted over 563,531 primitives from both recordings:

| Feature | Use | Native mapping |
|---|---|---|
| Blending | 6 equations: `(Cs−Cd)·As+Cd` 90%, `Cs·1.0+Cd` 10%, `Cs·As+Cd`, `(Cs−Cd)·Ad+Cd`, `(Cs−Cd)·1.0+Cd`, `Cd` | Fixed-function blending. PS2 alpha `0x80` is 1.0, so source alpha is scaled in the shader |
| Alpha test | `A ≥ 64` with AFAIL=FB_ONLY 87%; NEVER with FB_ONLY 12% | FB_ONLY: draw passing pixels with depth writes, failing pixels without. NEVER+FB_ONLY is depth writes off |
| Destination alpha test | 448 draws | Stencil built from the alpha bit |
| Depth | Z24, GEQUAL or ALWAYS | GPU depth test GEQUAL on a 24-bit depth buffer |
| Textures | 4-bit palettized (32-bit CLUT) 87%, 8-bit palettized 1%, 32/24-bit only for render-target reads | Decode once into RGBA8 textures, cached by VRAM page and CLUT |
| Mipmaps | About a third of textured draws sample up to 3 levels (MXL=2, LOD from Q with a fixed K); the rest use one level | Upload the game's mip levels (MIPTBP1) and compute the GS LOD in the shader |
| Render-target reads | Always the same layout: 32-bit read as 32-bit, or as 24-bit with alpha from TEXA | Bind the render target's GPU texture; for 24-bit reads replace alpha with TA0 |
| Fog | Vertex fog on 77% of draws | Lerp to FOGCOL in the shader |
| Minor | Dithering 43%, AA1 lines 1%, PABE 0.3%, color wrap instead of clamp 10% (additive passes) | Dither dropped at higher resolutions; the rest approximated |
| Not used | FBMSK, FBA, region clamp/repeat, palette or depth reinterpretation of render targets, GS→EE readback | — |

Everything maps onto ordinary GPU features. Nothing needs fragment-shader interlock or compute shaders.

## Game-side rendering

Mapped from the recompiled functions (`Port/generated/<name>_0x<address>.cpp`):

- **Frame loop.** The scheduler thread runs `execScript` priority groups each frame: `graphFrameInit` (P1),
  `DispDispObjGroupModel` for the scene (P9), `windowDraw` (P10), and `fadeScreen`, `drawDemoBar` and
  `graphFrameFinish` (P11). `iFinish` resolves texture uploads (`texTransResolve`) and `iosGPFrameInit` flips and
  sends the display list. The frame drawn is the one built in the previous iteration.
- **Display list.** One VIF1 DMA chain per frame, made of 32 ordered layers (`iosDlPushLayer` /
  `iosDlChainTailCurrent`); the layer index is the draw order. Geometry goes through VU1 (`XGKICK`), register
  writes through VIF `DIRECT`.
- **Models.** `modelDisp(DispModelClass*, matrices)` at `0x118C7F0` draws one model: it pushes its display-list
  layers, selects VU1 microcode per layer (`transMicroCode`: 0 default, 1 reflection, 2 water, 5–9 shadow and skin
  variants, 7 particles, `0xA` cloth) and requests its textures (`texTransRequest`). Terrain, characters and
  colossi all go through it via `DispLayoutObjModel`.
- **Effects.** Each effect is its own function with parameter getters: `shadowDisp`/`shadowDraw`,
  `depthOfField`, `feedbackBlur`, `cameraBlur`/`screenBlur`, `solarFlare`, `zFog`, `reflectionBegin/End`,
  `refractionBegin/End`, `waterRefractionBegin/End`, `bossDeathEffectBegin/End`, `screenBrightness`.
- **2D.** `windowDraw`, `spriteDrawSprite*`, `kPrintf*`, `gsReg*`.

## Design

The renderer is built in two layers.

### Layer 1: native GS backend

A new `GSRasterBackend` (`GSNativeBackend`, selected with `PS2X_GS_NATIVE=1`) consumes the same primitive stream
as the existing backends, so it covers the whole game without game-specific code:

- **VRAM shadow.** Uploads are applied to a CPU copy of VRAM with the existing swizzle code. It feeds texture
  decoding and savestates.
- **Texture cache.** Keyed by TBP, TBW, PSM, size and CLUT; entries are invalidated when an upload or a render
  target touches their pages. 4-bit and 8-bit textures are decoded to RGBA8 with their CLUT, including mip levels.
- **Render targets.** GPU color textures keyed by FBP/FBW/PSM, and depth buffers keyed by ZBP, at an internal
  scale factor. Rendering marks the covered VRAM pages as owned by the target. A texture read from those pages
  binds the target's GPU texture instead of decoding VRAM. Uploads over owned pages end that ownership.
- **Draw translation.** Consecutive primitives with the same state are batched into one draw. State maps to a
  small set of pipeline variants: blend equation, depth test, alpha-test mode (including the FB_ONLY split),
  destination-alpha stencil, fog, texture function and 24-bit read alpha. Positions subtract XYOFFSET and are
  multiplied by the scale; sprites become two triangles.
- **Present.** The display buffer's target is drawn straight to the window, without a RAM copy.
- **Savestates and readback.** Render targets are downloaded into the VRAM shadow only when the runtime needs
  VRAM (savestates, `SnapshotVram`, GS→EE transfers, which these scenes never use).

The existing backends stay as references: `gs_replay` compares a native replay against them.

### Layer 2: engine-level replacements

Once layer 1 is correct, individual parts move up to the game level, using the hook API in
`Port/src/sotc/function_hooks.h`:

- Post effects (`depthOfField`, `feedbackBlur`, `solarFlare`, `screenBlur`, `zFog`) as native shaders at full
  resolution, reading their parameters from the game's getters, instead of fixed 512×448 sprite chains.
- Shadows (`shadowDisp`/`shadowDraw`) at full resolution.
- Model drawing at `modelDisp`, decoding each model's VIF packets once into GPU buffers. This is the layer a port
  to a different platform needs, because it no longer depends on VU1 output.

The display-list layers are the composition boundary: anything not replaced keeps flowing through layer 1 in
layer order.

## Risks

- **Coverage.** Two scenes were surveyed. Colossus fights (fur, water, the death effects) may use features not
  seen yet, such as palette effects or depth reads. Every fight should be recorded and run through the census
  before the renderer replaces the default.
- **Upscaling.** The post-effect chains assume 512×448: half-texel offsets, ±3 px blur taps and 64-pixel
  column sprites. At higher scales they need adjusting per chain, which is the motivation for layer 2.
- **Rasterization rules.** GS coverage and texel-center rules differ slightly from GPU rules; small offsets are
  expected at scale 1 and tolerated by the comparison thresholds.
- **VRAM aliasing.** Scratch targets such as `150` reuse memory that also holds textures; the page-ownership
  model has to end ownership precisely on uploads.

## Milestones

| | Milestone | Done when |
|---|---|---|
| M0 | Frame census and game-side map | This document |
| M1 | Native backend in `gs_replay` at scale 1: texture cache, render targets, main-scene draws | Done: both recordings match the reference visually |
| M2 | All passes of both recordings: render-target reads, shadows, blur chains, FB_ONLY, destination alpha, fog | Presented frames match the reference within a PSNR threshold |
| M3 | In-game with `PS2X_GS_NATIVE=1`: presentation, savestates | Done on Linux (see Status); scenario harness not yet run |
| M4 | Internal resolution 2×–4× | Post-effect chains and HUD correct at each scale |
| M5 | Coverage: recordings and census of every colossus fight, water, fur and death effects | No unknown GS features; gaps closed |
| M6 | Layer 2: post effects and shadows at full resolution, then models | Per-effect switches, compared against layer 1 |

## Status

M0, M1 and M3 are done. M6 has opt-in native passes for screen brightness and six post effects (see below);
shadows and models remain pending.
`GSNativeBackend` (`External/PS2Recomp/ps2xRuntime/src/lib/gs/native/`) replays both
recordings in `gs_replay` and runs the game with `PS2X_GS_NATIVE=1` (see `Docs/CONFIGURATION.md`).

In the game on Linux, the window's context is created through EGL (a patch to raylib's GLFW platform, applied at
configure time) and the native backend's context joins its share group. Each `Present` copies the displayed area at
the internal resolution into a ring of three shared textures with fences (`GSSharedPresent`), which the window
draws. A colossus cutscene from a savestate on a Radeon 680M runs at 52–56 game frames/s at 2×, against 51–58 with
the GPU renderer at 1×. Savestates made under the native backend load back correctly, and the game boots to its
menus. Two things made the CPU side fast enough: reusing the resolved draw state while consecutive primitives
share it, and keeping decoded textures whose re-uploaded bytes did not change (the game re-uploads about 1 MB of
textures every frame).

| Recording | Presented frames vs the CPU reference | Replay time (CPU reference / native) |
|---|---|---|
| Riding, 10 fields | mean 1.1–4.2 levels, max 68–133 | 1.61 s / 0.48 s |
| Opening cutscene, 14 fields | mean 2.4–3.5 levels, max 12–30 | 2.29 s / 0.70 s |

Per pass, copies and downsamples are exact and the shadow volumes differ in 0.1% of pixels. The remaining
differences are rounding: GPU blending rounds where the GS truncates (`>> 7`), and bilinear weights differ slightly.
In the riding scene they accumulate through an effect that feeds the previous frame back, which is why the mean
grows from frame to frame. Rendering at 2× and 4× works, with post-effect chains not yet adapted (M4).

Lessons from M1:

- `GSCpuBackend` models the GS texture page cache, which serves a page's old bytes after a write until `TEXFLUSH`.
  The native backend forwards `TEXFLUSH` and flushes that cache after writing targets back, or decoded textures
  read stale bytes.
- The game streams textures into memory that scratch targets (`150`, `178`) also use, mid-frame. Page ownership has
  to come from the area actually drawn, not the scissor, and uploads take pages away from targets.
- Shadow volumes add `+1`/`−1` per covered pixel with COLCLAMP off, wrapping modulo 256. Order-independent, so a
  float accumulation folded back modulo 256 is exact.
- GS point sampling takes `floor(u)`; texture coordinates on exact texel edges need a small bias.

Not implemented yet: destination alpha test (448 draws per frame in these scenes), PABE, region repeat,
FBMSK, local-to-local transfers out of GPU targets.

### Continuation (2026-10-29)

Read the saved Claude sessions and resumed from the committed M3 implementation. Native texture sampling now
honors region-clamp (`WMS/WMT=2`) MIN/MAX bounds within the decoded texture, including inverted bounds, and
includes those bounds in the draw batching key. Region repeat (`WMS/WMT=3`) still warns and uses ordinary clamp.
Bounds outside the decoded texture are not covered by this change.

`gs_replay --check-native-region-clamp` draws a synthetic atlas of 36 cases and compares readback with the CPU
backend: U/V repeat, ordinary clamp and region clamp, nearest and bilinear filtering, and normal/inverted bounds.
It requires a working native OpenGL context and fails instead of accepting CPU fallback. All cases match exactly
on the Radeon 680M at 1×. Disabling the new region sampling makes the same test fail with 9,594
channels outside tolerance (maximum error 96), confirming it detects the original bug. The riding and cutscene replays retain the documented CPU/native differences above;
`--compare-backends` returns 1 because it checks exact equality, which this renderer does not yet achieve.
M2, M4 and wider gameplay coverage remain unfinished; this change makes no new performance claim.

## Tools

- Record: `PS2X_GS_RECORD=first:count:file` (fields; see `Docs/PROGRESS.md`) while playing or from a savestate
  loaded with `SOTC_DEBUG_SCRIPT="1:load:<state>"`.
- Dump: `gs_replay <recording.gsr> --text <dump.txt>`.
- Census: `python3 Tools/gs_frame_census.py passes <dump.txt> [frames]` and
  `python3 Tools/gs_frame_census.py features <dump.txt>...`.
- Compare presented frames: `gs_replay <recording.gsr> --compare-backends cpu native [--dump <dir>]`. Backends
  `native2`…`native8` render at that scale; with `--dump`, `*_native<N>_full.png` shows the displayed buffer at the
  internal resolution.
- Compare pass by pass: `gs_replay <recording.gsr> --pass-diff cpu native <fields>`, with `GS_PASS_DUMP=<dir>` to write
  images of the passes that diverge.

- Synthetic region-clamp regression: `gs_replay --check-native-region-clamp` (requires GPU access).

### M6 first effect: screen brightness (experimental)

`SOTC_NATIVE_BRIGHTNESS=1` marks the second sprite vertex emitted by `screenBrightness` (`0x0117F758`),
at its `gsReg` return address `0x0117F8A0`. The existing game function still computes its brightness color and
alpha, emits into layer 30, and restores GS state. Only that draw is replaced: a reserved host A+D register
(`GS_REG_HOST_BRIGHTNESS`, `0x7E`, XYZF2 payload) submits a semantic `ScreenBrightness` command through the
frontend, recording wrapper and graphics worker. This avoids issuing GPU work while the EE is still building
the next frame's display list. There is no persistent marker state or new savestate layout.

The native backend draws a fullscreen triangle with a dedicated color shader and the game's blend factors,
at the internal resolution, clipped to the original scissor. Prior draws are flushed first, later draws restore
their normal GL state, and target page ownership/writeback are updated. Only the known untextured, depth-independent
brightness state covering the scissor is eligible. Other variants and other backends submit the original sprite.
The switch defaults off. The neutral brightness value 8 emits no pass.

Recordings containing the effect use additive opcode 16 with an unchanged `GSPrimitiveBatch` payload. Updated
replay tools count and replay the effect, including on reference backends; older tools do not understand the new
opcode. Existing recordings keep their layout. `gs_replay --check-native-brightness` exercises frontend routing,
multiple brightness levels at 1×/2×/4×, draw ordering, the threaded queue and fallback behavior.

This establishes the ordered game-effect boundary for M6; it does not replace blur/glow chains, shadows or models,
and is not a performance optimization claim.

Validation on 2026-10-29 (Radeon 680M):

- `--check-native-brightness`: 19 checks pass. Captured RGB is exact against layer 1 at 1×. At 2×/4×,
  interior differences are bounded by 2 levels from layer 1's interpolation/truncation of constant sprite
  colors. Leading borders are tested against the brightness formula separately: layer 1's 15/32-pixel
  coverage shift leaves a 1- or 2-pixel strip at these scales, while the new fullscreen pass covers it.
  Queue and fallback VRAM comparisons at 1× are exact, including alpha.
- A live 2× run loads `field.state`, sets brightness to 4, executes the native pass, saves a private state,
  reloads it and exits normally. Captures before/after loading were inspected. The saved state and logs are
  in `build/runs/m6-brightness/`; user quick states and brightness configuration were not overwritten.
- The live recording contains opcode 16; replay on the native backend executes the shader. CPU/direct and
  CPU/threaded replays have identical presented pixels and VRAM at both field checkpoints.
- This recording also exposed a replay-tool lifetime bug with trailing queued draws. Comparison tools now
  keep VRAM alive until the backend workers have stopped, preventing the observed use-after-free crash.
- The region-clamp regression still passes all 36 tiles exactly. Existing riding output retains the
  previously documented CPU/native differences. These checks do not establish a speed improvement or
  whole-game coverage.

For this host-only build, the unchanged generated EE archive was reused through the existing
`SOTC_GAME_CODE_LIBRARY` CMake option. Guest CPU/context and recording batch layouts are unchanged.

### M6 post effects (experimental)

`SOTC_NATIVE_EFFECTS=1` enables brightness and six post effects together; each also has its own switch:
`SOTC_NATIVE_DOF`, `SOTC_NATIVE_FEEDBACK_BLUR`, `SOTC_NATIVE_SCREEN_BLUR`, `SOTC_NATIVE_SOLAR_FLARE`,
`SOTC_NATIVE_ZFOG`, `SOTC_NATIVE_FRONT_FOG`. `Port/src/sotc/native_effects.cpp` watches `gsReg` (`0x1C0928`) calls
whose return address lies inside the effect's function and, before the first register of a packet, appends a host
register `GS_REG_HOST_EFFECT` (`0x7D`) naming the effect; `gsRegEnd` closes the scope with effect 0. The game still
computes every parameter, texture, mask and order. Only the scope changes how a draw is executed.

Inside a scope, the native backend draws each rectangular image pass (a two-vertex sprite with no FBMSK, DATE or
scan mask on a 32-bit target) as one quad at the internal resolution with a dedicated shader: texture coordinates
interpolated from the sprite's corners, the game's blend, alpha test, fog and FBA, and a copy of the target when
it reads itself. Other primitives inside the scope use the normal path. Other backends ignore the tag.
`PS2X_GS_NATIVE_EFFECT_STATS=1` prints per-effect pass and fallback counts at exit, and `gs_replay
--original-effects` strips the tags from a recording.

Validation on 2026-10-29 (Radeon 680M):

- `gs_replay --check-native-post-effects`: 77 checks pass (six effects, 1×/2×/4×, texture, copy, blend, self-read,
  threaded queue and CPU fallback).
- Live recordings from `field`, `cut2450` and `slow` (scale 2 and 4, camera motion) and one with depth of field
  forced: screen blur, solar flare, zFog and front fog run natively (up to 712 solar flare passes in 40 fields)
  with no fallback. Replayed at 1×, presented frames with the tags differ from the same recording replayed with
  `--original-effects` by at most 1 level. Depth of field and feedback blur did not reach the tagged path in these
  recordings and are covered only by the synthetic checks.
