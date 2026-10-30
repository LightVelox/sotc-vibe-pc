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
| Destination alpha test | 448 draws | The shader reads the target's alpha bit at its own pixel |
| Depth | Z24, GEQUAL or ALWAYS | GPU depth test GEQUAL on a 24-bit depth buffer |
| Textures | 4-bit palettized (32-bit CLUT) 87%, 8-bit palettized 1%, 32/24-bit only for render-target reads | Decode once into RGBA8 textures, cached by VRAM page and CLUT |
| Mipmaps | About a third of textured draws sample up to 3 levels (MXL=2, LOD from Q with a fixed K); the rest use one level | Upload the game's mip levels (MIPTBP1) and compute the GS LOD in the shader |
| Render-target reads | Always the same layout: 32-bit read as 32-bit, or as 24-bit with alpha from TEXA | Bind the render target's GPU texture; for 24-bit reads replace alpha with TA0 |
| Fog | Vertex fog on 77% of draws | Lerp to FOGCOL in the shader |
| Minor | Dithering 43%, AA1 lines 1%, PABE 0.3%, color wrap instead of clamp 10% (additive passes) | Dither dropped; PABE and color wrap blend in the shader; AA1 ignored |
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
  destination-alpha read, fog, texture function and 24-bit read alpha. Positions subtract XYOFFSET and are
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
  column sprites. At higher scales they need adjusting per chain, which is the motivation for layer 2. (M4 handles
  them in layer 1 with general rules: whole-pixel sprites, 1× blur buffers, scaled-buffer reads in the shader.)
- **Rasterization rules.** GS coverage and texel-center rules differ slightly from GPU rules; small offsets are
  expected at scale 1 and tolerated by the comparison thresholds.
- **VRAM aliasing.** Scratch targets such as `150` reuse memory that also holds textures; the page-ownership
  model has to end ownership precisely on uploads.

## Milestones

| | Milestone | Done when |
|---|---|---|
| M0 | Frame census and game-side map | This document |
| M1 | Native backend in `gs_replay` at scale 1: texture cache, render targets, main-scene draws | Done: both recordings match the reference visually |
| M2 | All passes of both recordings: render-target reads, shadows, blur chains, FB_ONLY, destination alpha, fog | Done: all six recordings at least 43 dB PSNR at 1× (threshold 40 dB) |
| M3 | In-game with `PS2X_GS_NATIVE=1`: presentation, savestates | Done on Linux (see Status); scenario harness 16/16 (see M5) |
| M4 | Internal resolution 2×–4× | Done: post-effect chains and HUD line up with 1× at 2×, 3× and 4× (see M4 below) |
| M5 | Coverage: recordings and census of every colossus fight, water, fur and death effects | Done: 40 recordings, no unmapped GS features; all above 46.8 dB after the reference interpolation fix (see M5) |
| M6 | Layer 2: post effects and shadows at full resolution, then models | Per-effect switches, compared against layer 1 |

## Status

M0 to M5 are done. M6 has opt-in native passes for screen brightness and six post effects (see below);
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

Presented frames match the CPU reference at 1× above 40 dB PSNR in all six recordings (numbers and method under M2
below). At 2×–4× the post-effect chains and the HUD line up with the 1× image (M4 below).

Lessons from M1:

- `GSCpuBackend` models the GS texture page cache, which serves a page's old bytes after a write until `TEXFLUSH`.
  The native backend forwards `TEXFLUSH` and flushes that cache after writing targets back, or decoded textures
  read stale bytes.
- The game streams textures into memory that scratch targets (`150`, `178`) also use, mid-frame. Page ownership has
  to come from the area actually drawn, not the scissor, and uploads take pages away from targets.
- Shadow volumes add `+1`/`−1` per covered pixel with COLCLAMP off, wrapping modulo 256. Order-independent, so a
  float accumulation folded back modulo 256 is exact.
- GS point sampling takes `floor(u)`; texture coordinates on exact texel edges need a small bias.

Not implemented yet: alpha values above 0x80 in render targets (stored saturated at 0x80, see M2), dithering, AA1,
and 16-bit render targets (drawn as 32-bit).

### M2: all passes within a PSNR threshold

Measured first: `--pass-diff` and the census showed that most of the difference came from blend rounding, not from
missing features. In the riding scene the blur chains (`1e8`/`1fe`, 64×64) blend sprites at alpha 0x40, GPU blending
rounds where the GS truncates, and the error is added back to the whole screen and fed into the next frame. In the
opening cutscene about 1,000 fog-layer triangles per field (vertex alpha around 0x27, read of the `170` target)
each rounded up instead of down, about 6 levels too bright in total. The 448 destination-alpha draws are the depth
fog layers (32 sprites per frame, DATM=1, each layer clears bit 7 for the next); PABE is used by 110 refraction
triangles per field in the cutscene. FBMSK, region repeat and local-to-local transfers do not occur in these
recordings and are covered by synthetic checks only.

What the native backend now does:

- **Read-back draws.** The fragment shader can read the target at its own pixel (`texelFetch` on the attached
  texture after `glTextureBarrier`). It then applies the destination alpha test (bit 7 of the stored alpha),
  blends with the GS integer formula `((A − B) · C >> 7) + D` including PABE, COLCLAMP clamp or wrap and Ad,
  and merges FBMSK bit masks. A draw is read back when it uses DATE, PABE or an FBMSK that is not whole channels,
  or when it blends and the factor may be fractional: always for sprites, and for other primitives unless C is
  FIX 0 or 0x80, or As is known to be 0 or 0x80 (equal vertex alphas, and a texture whose decoded alpha is only
  0 and 0x80). Primitives of one batch whose interiors overlap (separating-axis test on a 16-pixel grid) start a
  new segment of the same draw after another barrier, so each pixel is read and written once per draw and
  batches are not split. A batch that samples its own target refreshes its copy per segment.
  `PS2X_GS_NATIVE_EXACT_BLEND` selects 0 none, 1 sprites, 2 (default) as above, 3 every blended primitive.
- **Whole-channel FBMSK** maps onto `glColorMask`.
- **Texel-exact sampling.** Where the bound texture has the logical resolution (decoded textures, and render
  targets at 1×) the shader fetches texels itself: wrap modes on integer coordinates, including region repeat
  (`(u & MINU) | MAXU`), and bilinear weights computed and rounded like the reference, with sprite UVs rounded
  to 1/16 texel. Render targets read at higher scales keep hardware sampling (region repeat there is drawn as
  clamp).
- **Depth** is rounded to Z24/Z16 steps in the shader (`gl_FragDepth`), so coplanar passes with GEQUAL behave like
  the GS instead of z-fighting (refraction water in the cutscene). Gouraud color and fog are evaluated at the GS
  sample point like the texture coordinates.
- **Local-to-local transfers** write targets back, run the copy in PS2 memory and reload the written pages of every
  live target from memory.
- The brightness pass (M6) blends in the shader the same way.
- Fixed while measuring: the first COLCLAMP=0 accumulation into a target copied the float buffer onto itself
  (creating a framebuffer rebinds both read and draw), losing the target's previous contents.

Presented frames against the CPU reference at 1×, Radeon 680M. The live recordings are replayed with
`--original-effects` (layer 1 only; with the M6 effect tags the layer-2 passes still use GL blending). Mean and
max are per field (absolute difference in levels), PSNR is the worst field, ">8" is the worst field's share of
pixels more than 8 levels off. Replay time is `--backend native --no-compare`, best of 5.

| Recording | Before M2: mean, max, PSNR, >8 | After: mean, max, PSNR, >8 | Replay 1× before / after | Replay 2× before / after |
|---|---|---|---|---|
| Riding (`ride.gsr`, 10 fields) | 1.14–4.20, 68–133, 32.9 dB, 32.0% | 0.01–0.41, 67–139, 47.8 dB, 0.09% | 0.29 / 0.32 s | 0.31 / 0.37 s |
| Opening cutscene (`cut.gsr`, 14) | 2.38–3.49, 12–30, 35.5 dB, 0.16% | 0.51–1.19, 4–33, 44.0 dB, 0.01% | 0.40 / 0.48 s | 0.43 / 0.57 s |
| Cutscene 2450 (`m6-effects-smoke/cut2`, 10) | 0.00–2.91, 0–22, 37.2 dB, 0.01% | 0.00–0.52, 0–16, 49.9 dB, 0.00% | 0.30 / 0.32 s | 0.35 / 0.40 s |
| Field (`m6-effects-smoke/field2`, 8) | 1.89–4.98, 70–89, 33.1 dB, 11.9% | 0.05–0.93, 67–88, 43.9 dB, 0.30% | 0.29 / 0.32 s | 0.29 / 0.33 s |
| Slow camera (`m6-effects-smoke/slow4`, 8) | 0.00–3.23, 0–76, 37.0 dB, 0.15% | 0.00–0.36, 0–74, 48.6 dB, 0.09% | 0.23 / 0.24 s | 0.25 / 0.27 s |
| Depth of field forced (`m6-effects-forced`, 16) | 0.00–5.84, 0–139, 30.6 dB, 35.3% | 0.00–1.00, 0–139, 43.0 dB, 0.35% | 0.39 / 0.40 s | 0.41 / 0.50 s |

`gs_replay <rec> --compare-backends cpu native --psnr-threshold 40` passes on all six. Replay cost of mode 2 is
3–20% at 1× and up to about 30% at 2× (the cutscene draws about 1,500 read-back segments per field);
mode 1 replays as fast as before M2 with PSNR 36.4 dB (cutscene) to 47.5 dB. At 2× the presented frames are compared
after downsampling, so they are a sanity check: the mean in the riding scene fell from 3.4–5.7 to 2.9–3.0 levels and
in the cutscene from 2.6–3.7 to 1.0–1.5.

Remaining differences: triangle edges and tips (GL samples 1/32 pixel from the GS sample point; the maxima are
isolated pixels, see the >8 column), perspective texture coordinates and Gouraud colors interpolated in a different order than
the reference's float barycentrics (±1 level, and different texels where a texture is heavily minified), bilinear
edges of 0/0x80 alpha textures on primitives that keep GL blending, and render-target alpha above 0x80, which is
stored saturated (the representation that lets GL use DST_ALPHA as the GS's Ad). Mipmapping is not used by either
backend. In the cutscene a fog layer drawn with Gouraud alpha still ends about 1.5 levels brighter than the
reference after all layers; the cause was in the reference (fixed in M5, below).

Synthetic checks, each comparing native readback with `GSCpuBackend` (all pass on the Radeon 680M; each fails
when its feature is disabled):

- `--check-native-blend`: all 81 blend selector combinations with COLCLAMP clamp and wrap, overlapping sprites and
  triangles, and fogged bilinear CT24 render-target reads; exact at 1×, sanity at 2×.
- `--check-native-date`: layered DATM=1 sprites that clear bit 7, DATM=0, overlapping triangles; exact at 1×.
- `--check-native-fbmsk`: whole-channel and bit masks with and without blending.
- `--check-native-pabe`: overlapping sprites and triangles on both sides of alpha 0x80, four equations.
- `--check-native-region-repeat`: region repeat on decoded textures and on a render target, nearest and bilinear.
- `--check-native-local-copy`: a transfer between two live targets, then a blend over and a texture read of the
  destination, at 1× and 2×.

FBMSK and PABE allow 16 mismatching pixels: triangle tips the two rasterizers cover differently. All other checks
are exact at 1×.

### Continuation (2026-10-29)

Read the saved Claude sessions and resumed from the committed M3 implementation. Native texture sampling now
honors region-clamp (`WMS/WMT=2`) MIN/MAX bounds within the decoded texture, including inverted bounds, and
includes those bounds in the draw batching key. (Region repeat and bounds outside the decoded texture were
added with M2, below.)

`gs_replay --check-native-region-clamp` draws a synthetic atlas of 36 cases and compares readback with the CPU
backend: U/V repeat, ordinary clamp and region clamp, nearest and bilinear filtering, and normal/inverted bounds.
It requires a working native OpenGL context and fails instead of accepting CPU fallback. All cases match exactly
on the Radeon 680M at 1×. Disabling the new region sampling makes the same test fail with 9,594
channels outside tolerance (maximum error 96), confirming it detects the original bug. The riding and cutscene replays retain the documented CPU/native differences above;
`--compare-backends` returned 1 because it checks exact equality; it now also takes a PSNR threshold (M2).

### M4: internal resolution 2×–4×

Measured first. `gs_replay --compare-backends` with a backend above 1× now compares the displayed buffer as the
window shows it (without the CRTC merge), box-filtered to 512×448, and no longer calls `Present` on that backend:
`Present` wrote the targets back into PS2 memory every field, so the next frame's reads of those buffers came from 1×
copies. Besides PSNR it prints a low-pass PSNR (differences of 4×4 block means: blind to edge aliasing, sensitive to
intensity and placement errors of more than a pixel or two) and a sample PSNR (only the stored pixel at each GS
pixel's sample point, which renders what 1× renders unless a pass reads a scaled buffer at a fractional position).
`--pass-diff` against `native<N>` locates the passes.

What was wrong:

- **Pixel mapping.** Vertices were moved by 0.5 − 1/32 of a GS pixel at every scale, so stored pixel j of an N×
  buffer sampled the GS position (j + 0.5)/N − 0.5, centered on the GS sample point. A sprite from x0 to x1 then
  covered half of GS pixel x0 and half of x1: full-screen passes never wrote the first stored row and column of a
  buffer (a dark line along the top and left of the frame, and stale pixels fed into the blur chains), and the
  half-pixel-offset sprites of the upsampling passes (−0.5 to 127.5) left the last stored row and column stale. A
  nearest copy whose texture coordinates equal the position (the final copy `e0` → `0`) read half of every pixel from
  its neighbor: each such pass moved the image by half a GS pixel. In the riding scene at 2× the copy doubled the
  difference to 1× (mean 1.7 → 3.6 levels in `--pass-diff`).
- **Blur extent.** The glow, blur and depth-of-field chains downsample into 256-, 128- and 64-pixel-wide buffers
  (`150`, `170`, `1c0`, `1e0`, `1e8`, `1fe`) and upsample back. At N× those buffers had N times the resolution, so
  every blur spread over N times fewer screen pixels.
- **Render-target reads above 1×** used GL's sampler on the scaled texture: `GL_REPEAT` over the buffer's size instead
  of the texture's, and region repeat drawn as clamp.
- **Write-back** above 1× was a bilinear blit, so PS2 memory (savestates, `--pass-diff`, textures decoded from a
  buffer's pages) held a resampled image rather than the GS samples.

What the backend does now (`gs_native_backend.cpp`; nothing game-specific):

- Stored pixel j of a buffer at scale N samples the GS position j/N: vertices move by (0.5 − 1/32)/N, the same shift
  as before at 1×. GS pixel x covers the positions [x, x + 1), and its top-left stored pixel holds exactly what 1×
  renders there. Attributes keep the 1/32-stored-pixel correction.
- Sprites cover whole GS pixels: their edges move to the boundaries of the pixels whose sample points they contain
  (`ceil`), and their texture coordinates are extended along the same slope, in the normal path and in the M6 image
  passes.
- Buffers narrower than 512 pixels stay at 1× (`PS2X_GS_NATIVE_SCALE_MIN_WIDTH`, see `Docs/CONFIGURATION.md`); depth
  buffers follow their frame buffer. In the game that keeps every blur buffer at 1× and scales `e0`, `0`, `178`
  (shadow volumes) and the 512-wide use of `150`. The chains then run as at 1× (the downsampling passes read `e0` at
  the GS sample points) and are added back into the scaled `e0` with the game's bilinear upsampling.
- Scaled buffers are read in the shader at their own resolution: GS texel k is stored at N·k + sub, at positions
  k + sub/N. Nearest takes the stored texel containing the coordinate; bilinear interpolates between the stored
  texels around coordinate − 0.5. Wrap modes act on the GS texel and keep the sub-texel: repeat over the texture size,
  clamp, region clamp and region repeat.
- Nearest-sampled sprites show in each GS pixel the texel the GS picks at its sample point, and spread the pixel's
  stored pixels within that texel (over several texels when minified). Without this, sprites with half-texel offsets
  (the HUD frames sample at u = k + 0.5) would put texel boundaries in the middle of GS pixels under the new mapping.
  HUD elements look like the 1× image scaled up, and a nearest copy between scaled buffers copies every stored pixel.
- Lines and points are N stored pixels wide and centered on the GS pixels they cover.
- Write-back above 1× copies the top-left stored pixel of each GS pixel, so memory holds the 1× samples.

Presented frames, native N× against native 1× (box-filtered; "sample" is the sample PSNR), Radeon 680M. Before and
after are mean absolute difference (levels), minimum PSNR and minimum low-pass PSNR over the recording's fields; the
minimum sample PSNR is after M4. The four live recordings are replayed with `--original-effects`:

| Recording | 2× before → after | 3× before → after | 4× before → after | Sample 2× / 3× / 4× |
|---|---|---|---|---|
| Riding (`ride.gsr`) | 3.63, 30.4, 38.7 → 1.90, 34.3, 44.1 | 2.57, 33.0, 42.3 → 2.32, 33.0, 42.6 | 3.50, 30.8, 39.7 → 2.54, 32.4, 41.8 | 47.0 / 46.9 / 46.2 |
| Opening cutscene (`cut.gsr`) | 0.92, 37.3, 44.0 → 0.42, 47.5, 58.9 | 0.67, 40.5, 47.4 → 0.49, 46.4, 58.2 | 0.89, 37.4, 44.0 → 0.52, 45.9, 57.6 | 63.2 / 62.3 / 62.3 |
| Cutscene 2450 (`cut2`) | 0.57, 38.6, 44.7 → 0.21, 50.1, 60.2 | 0.37, 42.0, 48.0 → 0.26, 48.9, 58.5 | 0.56, 38.7, 44.7 → 0.27, 48.5, 58.2 | 54.1 / 51.8 / 50.6 |
| Field (`field2`) | 7.10, 26.3, 36.0 → 3.68, 29.8, 40.3 | 5.35, 28.6, 39.1 → 4.25, 28.7, 39.4 | 6.79, 26.9, 37.0 → 4.53, 28.3, 38.8 | 42.0 / 42.2 / 41.4 |
| Slow camera (`slow4`) | 2.95, 31.6, 39.7 → 1.13, 35.5, 45.6 | 1.86, 34.1, 43.7 → 1.37, 34.2, 44.0 | 2.88, 32.0, 40.9 → 1.50, 33.6, 43.2 | 48.3 / 48.4 / 47.7 |
| Depth of field forced (`m6-effects-forced`) | 8.20, 25.0, 34.2 → 4.65, 28.9, 39.1 | 6.03, 27.3, 37.4 → 5.30, 27.8, 38.1 | 7.81, 25.5, 35.1 → 5.63, 27.3, 37.5 | 40.4 / 40.7 / 40.0 |

Before M4, 3× looked better than 2× and 4× because at odd scales the centered mapping has a stored pixel on the GS
sample point. What remains is the higher resolution itself: the field and riding scenes are dominated by minified
ground textures, which alias differently at each resolution (neither backend uses mipmaps), and 3D geometry, sampled
over [x, x + 1) instead of at x, sits on average (N − 1)/2N of a pixel up and left of the 1× image, consistently with
every effect computed from it. With all buffers scaled (`PS2X_GS_NATIVE_SCALE_MIN_WIDTH=0`) the low-pass PSNR is
0.3–0.7 dB lower (riding 2×: 43.5 dB, forced depth of field 2×: 38.6 dB), and at 4× the depth-of-field blur on the
distant cliffs of the forced recording almost disappears, where the default keeps the 1× blur. The M6 tagged replays
(`cut2`, `field2`, `slow4`, `forced` with their effect tags) give the same numbers within 0.5 dB. Two fight
recordings from M5 (colossus 3 and 4) and a fight with the HUD shown (colossus 1, R1 held: health bar, stamina
circle and grip bar, recorded at 4×) measure 1.0–2.7 levels, low-pass 40.9–48.5 dB at 2× and 4×, and pass the 40 dB
threshold against the CPU reference at 1×.

Inspected at 2× and 4× against the 1× frame scaled up (crops of `--dump` output): tree glow and sky in the riding
scene (no line at the top and left edges, no doubled branches), the distant depth-of-field blur and the "Press △ to
Jump" message in the forced recording, the sun-lit cliff and fog in `field2`, and the colossus 1 HUD: the health bar
frame lines up with the 1× image row for row (before M4 a dark line ran along the top and left screen edges above
it), and the stamina circle and grip bar are smooth, without gaps or offsets.

At 1× nothing changed: presented frames of the riding, forced and `cut2` (tagged) replays are byte-identical to the
M2 build, and the six recordings keep the M2 PSNRs against the CPU reference.

Checks (all pass):

- `--check-native-scale` (new): sprites from a decoded texture (nearest, bilinear at texel centers, half-pixel
  offsets), a Gouraud triangle, a nearest copy and a region-repeat read between scaled buffers, a point-sampled
  downsample into a 128-pixel buffer and the half-pixel upsample back, drawn at 1× and at 2×, 3×, 4×. Sample points
  must match 1× (8 triangle-edge pixels allowed; Gouraud colors and what is computed from them within 2 levels),
  nearest and untextured sprites must fill
  whole GS pixels, the copy must reproduce every stored pixel, and the narrow buffer must stay at 1×. It fails with
  the old vertex shift, without the nearest-sprite rule, and with `PS2X_GS_NATIVE_SCALE_MIN_WIDTH=0`.
- `--check-native-blend`, `--check-native-date` and `--check-native-local-copy` at 2× scale their 128- and
  256-pixel buffers (the checks set `PS2X_GS_NATIVE_SCALE_MIN_WIDTH=0`) and compare exactly instead of as a sanity
  check, since write-back keeps the sample points: the blend cells and the local copy are exact, DATE has at most 16
  triangle-edge pixels. The bilinear reads of a scaled buffer in the blend check show the
  finer resolution and are not compared at 2×.
- `--check-native-brightness` and `--check-native-post-effects` compare whole images at 2× and 4×; they used to skip
  the one- or two-pixel leading border that layer 1 left unwritten.

Replay time (`--backend native<N> --no-compare`, best of 5) is unchanged within noise at every scale; M2 build / M4
build, in seconds:

| Recording | 1× | 2× | 3× | 4× |
|---|---|---|---|---|
| Riding | 0.31 / 0.32 | 0.33 / 0.34 | 0.47 / 0.47 | 0.57 / 0.56 |
| Opening cutscene | 0.45 / 0.43 | 0.58 / 0.58 | 0.74 / 0.78 | 1.02 / 1.04 |
| Cutscene 2450 | 0.31 / 0.34 | 0.42 / 0.40 | 0.51 / 0.51 | 0.68 / 0.66 |
| Field | 0.30 / 0.30 | 0.35 / 0.36 | 0.42 / 0.41 | 0.49 / 0.51 |
| Slow camera | 0.24 / 0.25 | 0.28 / 0.28 | 0.30 / 0.32 | 0.39 / 0.37 |
| Depth of field forced | 0.45 / 0.42 | 0.46 / 0.48 | 0.57 / 0.57 | 0.76 / 0.73 |

Not covered: depth of field and feedback blur through the M6 tagged path (no recording reaches it, as before), the
scenario harness, and the platforms that present through PS2 memory, where `Present` still writes the targets back
every frame (reads of the previous frame then come from the 1× copy). Two native backends in one process (as in
`--compare-backends native native4`) share whichever GL context was created last; this works because both create
the same objects in the same order, and `--check-native-scale` renders its scales one after the other.

### M5: coverage of the colossus fights

Details, commands and per-recording data are in `build/runs/m5/REPORT.md` (Radeon 680M).

- Scenario harness (`Tools/scenario.py colossi`, `PS2X_GS_NATIVE=1` at 2×): 16/16 pass (every colossus killed and
  marked dead, no crash or hang, logic stalls of at most 8 fields). The default renderer passed 15/16; c04 aborted with an
  amdgpu GPU hang during the death sequence and passed when rerun alone. With two instances at once native 2× averaged
  14.8 game frames/s against 22.9 for the default renderer at 1× (c04 alone: 18.3 against 22.7).
- 40 live recordings under `PS2X_GS_NATIVE=1`: both fight windows of all 16 colossi and 8 windows of two death
  sequences (c02 on land, c07 in water); water in c07, c12 and the c07 death, fur close-ups in c06 and the c02 death. The
  census found no GS feature the native backend does not map, and no `[gs-native]` warnings. Features not in the
  earlier recordings, all covered: `(0−Cs)·As+Cd`, `(Cs−Cs)·FIX+Cd`, `(Cs−Cd)·FIX+Cd` with FIX changing per frame,
  DECAL with TCC 1, AFAIL=KEEP, GEQUAL with ZMSK=1, region repeat on decoded textures, MMIN=2.
- Remaining gaps: mipmaps (MXL=1, MMIN=3 in c04, 4,110 and 9,393 draws) are drawn from level 0 by both backends, so the
  comparison cannot see them; a CT24 target read as CT32 (4–5 draws in c16 and the c07 death; the GS keeps alpha under
  CT24 writes, native stores the shader's alpha) is unverified, with no visible effect; AA1 and DTHE are not
  implemented and occur in every fight; a native shutdown segfault (exit −11) after the last frame in 2 of about 75
  runs, not reproduced in 4 reruns.

Accuracy: 38/40 recordings passed `--compare-backends cpu native --psnr-threshold 40`; c04_b (39.49 dB) and
death_c07_0240 (39.67 dB) failed. In those and in the other low recordings one late `e0` pass of stacked low-alpha
layers ended 1–2 levels brighter in native: large fogged triangle fans of 4-bit textures, MODULATE, vertex alpha 0–11,
blended additively or with `(Cs−Cd)·As+Cd`, hundreds of layers per pass. The same as the cutscene fog layer of M2.

Cause: the reference, not native. `GSCpuBackend`, the GPU emulation (the default renderer, which mirrors it) and
`GSReferenceBackend` interpolated triangle attributes as `a0·w0 + a1·w1 + a2·w2` in float. When the three vertices
have the same value the sum can land just below it (5.9999995 for alpha 6), and the truncation takes one level less on
part of the pixels: alpha 5 instead of 6 drops a sixth of the layer, fog 67 instead of 68 adds fog color. The GS DDA
keeps an attribute that is equal at all vertices exact (zero gradient), and so does GL's interpolation in native. Over
dozens of layers the reference lost 1–2 levels.

Fix: attributes are interpolated relative to vertex 0, `a0 + (a1 − a0)·w1 + (a2 − a0)·w2` (`gsTriangleLerp` in
`gs_triangle_rules.h`), for color, alpha, fog, UV and STQ, in `GSCpuBackend` (fast and per-pixel paths), the GPU
emulation shader and `GSReferenceBackend`. Lines already did this; sprites use the last vertex. The native backend is
unchanged. The GPU emulation now matches `GSCpuBackend` almost exactly (`--compare-backends cpu gpu`: riding 53.7 →
81.6 dB, cutscene 49.2 → 88.2 dB, forced depth of field 51.1 → 80.0 dB, mean 0.17–0.42 → 0.000 levels), so most of
their historical raster differences were this. `--lockstep` (cpu against ref) passes on the riding recording.

`gs_replay --check-native-fog-layers` draws 48 layers of such a quad (two triangles, bilinear 4-bit CLUT texture with
alpha 0–0x80, magnified, slight perspective, fog 68–115, vertex alpha 5–6) over a background in eight variants: as the
game, `(Cs−Cd)·As+Cd`, no fog, nearest, UV coordinates, untextured, flat shading, vertex alpha 0x80. It requires a mean
native − cpu difference of at most 0.05 levels in every variant and at most 16 channels off by more than one level
except in the nearest and UV variants (where a coordinate within float rounding of a texel edge may pick the other
texel). With the old interpolation it fails in seven variants (means of +0.2 to +1.06 levels in six of them, and
the flat variant through its fog); now the means are within ±0.013.

Presented frames against the CPU reference at 1× (minimum PSNR over the fields; mean absolute difference):

| Recording | Before | After |
|---|---|---|
| Riding (`ride.gsr`) | 47.84 dB, 0.371 | 50.52 dB, 0.026 |
| Opening cutscene (`cut.gsr`) | 44.03 dB, 0.735 | 63.90 dB, 0.006 |
| Cutscene 2450 (`cut2`, `--original-effects`) | 49.88 dB, 0.341 | 68.69 dB, 0.003 |
| Field (`field2`, `--original-effects`) | 43.89 dB, 0.599 | 46.35 dB, 0.088 |
| Slow camera (`slow4`, `--original-effects`) | 48.60 dB, 0.225 | 51.37 dB, 0.016 |
| Depth of field forced (`--original-effects`) | 43.03 dB, 0.808 | 45.19 dB, 0.121 |

All 40 M5 recordings now pass, the lowest at 46.80 dB (minimum PSNR, fight a / fight b, before → after):

| Colossus | Before | After | Colossus | Before | After |
|---|---|---|---|---|---|
| c01 | 51.38 / 52.88 | 53.33 / 55.55 | c09 | 52.55 / 48.09 | 57.78 / 53.30 |
| c02 | 45.21 / 44.78 | 49.01 / 47.84 | c10 | 45.06 / 43.16 | 49.86 / 48.29 |
| c03 | 47.08 / 47.75 | 48.45 / 49.37 | c11 | 51.23 / 49.24 | 57.40 / 52.01 |
| c04 | 40.44 / 39.49 | 47.79 / 47.19 | c12 | 47.33 / 43.56 | 53.25 / 52.44 |
| c05 | 53.06 / 48.08 | 56.96 / 53.96 | c13 | 47.25 / 44.99 | 53.18 / 49.98 |
| c06 | 43.63 / 44.49 | 64.75 / 51.17 | c14 | 49.85 / 49.33 | 54.76 / 52.53 |
| c07 | 40.52 / 43.95 | 54.33 / 56.45 | c15 | 49.54 / 52.37 | 56.00 / 57.20 |
| c08 | 48.84 / 48.93 | 51.28 / 51.46 | c16 | 49.79 / 46.49 | 56.16 / 52.15 |

Death windows: c02 0120/0660/1630/2060 = 47.00/46.80/44.87/46.52 → 47.85/49.36/46.80/47.63 dB; c07
0240/0480/0780/1200 = 39.67/42.18/49.26/49.20 → 56.73/59.83/60.80/66.19 dB. Mean absolute difference per recording
went from 0.12–1.27 to 0.005–0.051 levels.

At 2× native is unchanged (`--compare-backends native native2`: riding 34.31 dB, low-pass 44.05, sample 47.01;
cutscene 47.48, 58.92, 63.18). Against the CPU reference at 2× the cutscene went from 42.15 to 47.48 dB (low-pass 43.80
→ 58.87) and riding from 34.27 to 34.26 dB (low-pass 43.35 → 43.93, sample 45.92 → 47.48). With the M6 effect tags
(layer-2 passes still blend in GL) `cut2` went from 45.37 to 50.05 dB, `field2` 34.30 → 34.66, `slow4` 38.83 → 39.06,
forced 31.02 → 30.98.

Replay time (best of 5, wall time including loading): native unchanged; `GSCpuBackend` 2–3% slower (riding 1.59 →
1.62 s, cutscene 2.30 → 2.34 s); GPU emulation unchanged (riding 0.40 s, cutscene 0.67–0.69 s).

Not done: the GS goldens (`Analysis/oracle/gs_golden`, not in this checkout) record `GSCpuBackend` output and need
re-recording for this intended change; `ps2x_tests` does not link in `build/port` (missing `ps2_analyzer_lib`). The
default renderer's fog and spray layers are now 1–2 levels brighter in these scenes. A live harness run of c07 with the
default renderer (`PS2X_GS_NATIVE=0`, one instance) passes at 26 game frames/s and 43 drawn/s, and its captures look
right. An earlier run of the same binary (two instances, after an amdgpu ring reset) presented 1 frame/s and aborted
c04 and c07; the GPU then hung repeatedly. GPU replay times of c04_b and c07_a are unchanged (`--backend gputhread`
0.21–0.23 s), so that slowdown came from the GPU state, not from this change.

## Tools

- Record: `PS2X_GS_RECORD=first:count:file` (fields; see `Docs/PROGRESS.md`) while playing or from a savestate
  loaded with `SOTC_DEBUG_SCRIPT="1:load:<state>"`.
- Dump: `gs_replay <recording.gsr> --text <dump.txt>`.
- Census: `python3 Tools/gs_frame_census.py passes <dump.txt> [frames]` and
  `python3 Tools/gs_frame_census.py features <dump.txt>...`.
- Compare presented frames: `gs_replay <recording.gsr> --compare-backends cpu native [--dump <dir>]`. Backends
  `native2`…`native8` render at that scale; with `--dump`, `*_native<N>_full.png` shows the displayed buffer at the
  internal resolution. With a backend above 1× both sides are compared as displayed (no CRTC merge), box-filtered to
  the logical resolution, with low-pass and sample PSNR (see M4).
- Compare pass by pass: `gs_replay <recording.gsr> --pass-diff cpu native <fields>`, with `GS_PASS_DUMP=<dir>` to write
  images of the passes that diverge.

- Compare against a threshold: `--compare-backends <a> <b> --psnr-threshold <dB>` prints PSNR and the share of pixels
  more than 8 levels off per field, the minimum PSNR, and exits 1 when a field is below the threshold.
- Find a diverging draw: `GS_PASS_DRAWS=<n>` ends a `--pass-diff` pass every n draws; `GS_PASS_DUMP_ALL=1` also dumps
  passes that differ by at most 16 levels. `PS2X_GS_NATIVE_EFFECT_STATS=1` prints batch and read-back segment counts.

- Synthetic region-clamp regression: `gs_replay --check-native-region-clamp` (requires GPU access).
- Synthetic M2 checks: `--check-native-blend`, `--check-native-date`, `--check-native-fbmsk`, `--check-native-pabe`,
  `--check-native-region-repeat`, `--check-native-local-copy` (see M2 in Status).
- Synthetic M4 check: `--check-native-scale` (see M4).
- Synthetic M5 check: `--check-native-fog-layers` (see M5).

### M6 first effect: screen brightness (experimental)

`SOTC_NATIVE_BRIGHTNESS=1` marks the second sprite vertex emitted by `screenBrightness` (`0x0117F758`),
at its `gsReg` return address `0x0117F8A0`. The existing game function still computes its brightness color and
alpha, emits into layer 30, and restores GS state. Only that draw is replaced: a reserved host A+D register
(`GS_REG_HOST_BRIGHTNESS`, `0x7E`, XYZF2 payload) submits a semantic `ScreenBrightness` command through the
frontend, recording wrapper and graphics worker. This avoids issuing GPU work while the EE is still building
the next frame's display list. There is no persistent marker state or new savestate layout.

The native backend draws a fullscreen triangle with a dedicated shader that reads the target at each pixel and
blends the game's color with the GS's integer rounding (since M2), at the internal resolution, clipped to the
original scissor. Prior draws are flushed first, later draws restore
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
  colors. Leading borders are tested against the GS brightness formula (`(255 * alpha) >> 7`) separately: layer 1's 15/32-pixel
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
