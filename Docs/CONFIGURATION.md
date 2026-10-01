# Configuration

The port creates `sotc.ini` beside `sotc.exe` on first launch. With `run.bat`, the file is
`build/port/bin/sotc.ini`. Edit it while the game is closed; settings take effect on the next launch.

```ini
[Settings]
PS2X_MEMCARD=1
PS2X_INVERT_RIGHT_STICK=xy
PS2X_MOUSE_SENSITIVITY=1.0
PS2X_MOUSE_VERTICAL_SENSITIVITY=1.0
PS2X_GS_GPU=1
PS2X_GS_THREAD=1
PS2X_GS_DIRECT_PRESENT=1
PS2X_GS_COMPACT_QUEUE=1
PS2X_GS_QUEUE_CHUNKS=8
PS2X_VIF_SIMD_UNPACK=1
PS2X_VU1_SIMD_CLIP=1
PS2X_MTVU=1
PS2X_VU1_RECOMP=1
SOTC_VIDEO_MODE=NTSC
SOTC_WIDESCREEN=1
SOTC_WINDOW_MAXIMIZED=1
SOTC_VSYNC=1
PS2X_BIND_UP=UP
PS2X_BIND_DOWN=DOWN
PS2X_BIND_LEFT=LEFT
PS2X_BIND_RIGHT=RIGHT
PS2X_BIND_CROSS=F
PS2X_BIND_CIRCLE=MOUSE_RIGHT
PS2X_BIND_SQUARE=MOUSE_LEFT
PS2X_BIND_TRIANGLE=SPACE
PS2X_BIND_L1=Q
PS2X_BIND_R1=LEFT_SHIFT
PS2X_BIND_L2=LEFT_CTRL
PS2X_BIND_R2=RIGHT_SHIFT
PS2X_BIND_START=ENTER
PS2X_BIND_SELECT=TAB
PS2X_BIND_L3=H
PS2X_BIND_R3=G
```

Set `PS2X_MEMCARD=0` to run without a memory card. The slot 1 card image is stored at
`memcards/Mcd001.ps2` beside the executable. `PS2X_INVERT_RIGHT_STICK` accepts `0`, `x`, `y`, or
`xy`. `PS2X_GS_GPU`, `PS2X_GS_THREAD`, `PS2X_MTVU`, and `PS2X_VU1_RECOMP` accept `0` or `1`.

`SOTC_VSYNC=1` synchronizes the host window to the monitor's refresh rate, without a second
60 Hz software limiter. On monitors above 60 Hz, input is sampled at the display cadence while
the emulated game retains its own NTSC/PAL timing. Set `SOTC_VSYNC=0` to disable VSync and use
a 120 Hz host window and input polling limit. This can reduce input delay but can show tearing.
Existing INI files gain this setting on launch.

The experimental `PS2X_GS_DIRECT_PRESENT=1` path shares OpenGL textures between the GS worker
and the window. CRTC output stays on the GPU: its buffer becomes a texture through a GPU pixel-buffer
transfer, with no completed-frame copy to RAM or window texture upload from RAM. The window polls
producer fences, takes the newest completed texture, and protects textures still used by window draws
with consumer fences. Presentation requests are queued without draining the GS worker. If WGL sharing
is unavailable, startup reports the fallback to RAM presentation. Set it to `0` for the previous path.

`PS2X_GS_COMPACT_QUEUE=1` sends draw state only when its bytes change and sends vertices separately.
The GS worker reconstructs the original primitive batches in order. `0` restores complete batches.
`PS2X_GS_QUEUE_CHUNKS=8` limits outstanding GS command chunks (normally about 64 KB each) to bound
CPU submission backlog. It accepts 1 through 256; `256` restores the previous limit. This bounds CPU
commands, not the driver's GPU queue, and needs a gameplay comparison for the best overlap on this PC.

`PS2X_VIF_SIMD_UNPACK=1` expands unmasked 8/16/32-bit VIF vectors and applies row addition using
SSE integer operations. Masked and fill-mode operations keep their existing paths. `0` restores the
previous scalar fast unpacker. `PS2X_VU1_SIMD_CLIP=1` uses SIMD integer comparisons for compiled VU1
clip flags; `0` restores scalar comparisons. FMAC arithmetic, exceptional values, pipeline timing,
XGKICK timing, and the generated game logic retain their existing implementations.

Missing INI settings are applied on the first launch as well as appended to the file. Scripted gameplay,
runtime/VU checks, GPU queue comparisons, and window/save-state checks are recorded in
[PERFORMANCE_RESULTS.md](PERFORMANCE_RESULTS.md). See [PERFORMANCE.md](PERFORMANCE.md)
for separate comparisons, correctness checks, heavy scenes, and rollback instructions.

Linked GPU shaders are cached in `<exe dir>/game_data/gs_shaders`. Entries require the same exact
shader source and driver identity and a valid binary checksum; failed entries are compiled from source.
`PS2X_GS_SHADER_CACHE=0` disables the cache. `PS2X_GS_SHADER_CACHE_DIR=<directory>` overrides its
location. The first uncached launch or a driver/shader change can still require shader compilation.

`PS2X_FRAME_TIMES=<absolute CSV path>` records host swap intervals and submission/swap-call time.
On the shared path it also records the completed texture sequence. Optional `PS2X_FRAME_HASH=1`
fingerprints the pixels and identifies repeated images: the shared path reduces the image on the GPU
and reads eight bytes after its fence; the RAM path hashes its existing CPU pixels. Leave hashing off
for primary performance comparisons, because it adds work. Swap return times do not measure physical
monitor scanout or end-to-end input latency. The game scheduler update counter and its timer clock
must be measured separately from VBlanks; the capture tools do this. `drawn/s` remains the number of
presentation intervals containing primitive submissions, not proof of distinct gameplay images.

The asynchronous GPU readback polls completed frames without waiting for a busy readback slot.
When all slots are busy, the window keeps the most recent completed image. `PS2X_GS_GPU_STATS=1`
reports `readback skips` for this condition. VSync and readback changes do not increase the game's
simulation frame rate or remove its original movement and animation easing.

`PS2X_VU1_HOST_PACING` defaults to `1` in this port. While the VU worker is busy, the game
continues polling the device and advances timers and audio according to the elapsed host time.
Heavy scenes can draw fewer new frames instead of slowing the game and starving playback.
This adapts graphics timing to the host and is not a cycle-exact PS2 timing mode. Set it to `0`
to restore the original VU timing path. It requires MTVU and enabled VU1 timing.

`PS2X_EE_HOST_PACING` also defaults to `1` in this port. When the emulated EE falls more than half
a field behind the host clock, the scheduler advances EE time (timers, VBlanks and the IOP/audio
clock) to catch up, so the game drops frames instead of running in slow motion with audio gaps.
It requires MTVU, so the deterministic `PS2X_MTVU=0` mode is unaffected. Set it to `0` to let the
game slow down instead.

Click the game window to lock the cursor and move the camera with the mouse. Press Esc or switch
away from the window to release the cursor. `PS2X_MOUSE_SENSITIVITY` is a positive multiplier;
increase it for faster camera movement or decrease it for slower movement. The right stick inversion
setting applies to the mouse as well. `PS2X_MOUSE_VERTICAL_SENSITIVITY` adjusts vertical movement
relative to horizontal movement; its default of `1.0` gives equal angular sensitivity on both axes.
If an older INI file contains `PS2X_MOUSE_VERTICAL_SENSITIVITY=1.5`, change it to `1.0` for equal sensitivity.
The free camera uses fixed yaw and pitch changes per mouse pixel. Horizontal movement preserves
pitch, and diagonal movement uses the same sensitivity on both axes. Mouse deltas accumulate
between game updates and are consumed once, without joystick acceleration, elevation-dependent
speed scaling, or a frame-time multiplier. Mouse movement does not also drive the emulated right
stick during free look. The original camera positions the view, handles collision and follows
the character, while mouse control owns the final view direction and keeps the horizon level.
Camera corrections do not change the mouse's stored angles. Pitch is limited to 85 degrees above
or below the horizon without wrapping; reversing mouse direction moves away from either limit
immediately. The default rotation is approximately 0.143 degrees per mouse pixel.
Controller input, scripted cameras, and aiming modes use the original camera logic.
Set `SOTC_NATIVE_MOUSE_CAMERA=0` to restore mouse-to-stick camera control.

`PS2X_BIND_*` settings bind a keyboard key or mouse button to each PS2 button on controller 1.
Square attacks with the left mouse button, Circle raises the sword with the right mouse button,
Triangle jumps with Space, Cross uses F, and R1 uses Left Shift. The first click after the cursor
is released locks it without activating the bound game action. Existing INI files gain any missing
binding lines when the game next starts; existing settings are preserved.

Binding values are case-insensitive. Use a letter, digit, `F1` through `F12`, `UP`, `DOWN`, `LEFT`,
`RIGHT`, `SPACE`, `BACKSPACE`, `ENTER`, `TAB`, `LEFT_SHIFT`, `RIGHT_SHIFT`, `LEFT_CTRL`,
`RIGHT_CTRL`, `MOUSE_LEFT`, `MOUSE_RIGHT`, or `MOUSE_MIDDLE`. `SHIFT` and `CTRL` accept either
side, and `NONE` removes a keyboard or mouse binding. Controller bindings are unchanged.

Set `SOTC_VIDEO_MODE=PAL` for 50 Hz or `SOTC_VIDEO_MODE=NTSC` for 60 Hz. NTSC is the
default when the setting is absent. The game applies this choice at startup and skips the
50/60 Hz selection screen.

`SOTC_WIDESCREEN=1` adapts the game's native camera projection to the window's aspect ratio and compensates the
horizontal coordinates of HUD sprites, menu sprites, and text to retain their proportions.
Set it to `0` for the original 4:3 view. The INI choice overrides the in-game widescreen option.
Widescreen fills the entire window in both windowed and fullscreen modes. Resizing the window
adjusts the camera's horizontal field of view and UI compensation without stretching either.
With widescreen disabled, the original 4:3 view has black bars when needed. Boot screens retain
4:3 until the game uses its camera projection. The INI choice is reapplied when the camera updates,
including after loading a save state; a saved display option does not override it.

`SOTC_WINDOW_MAXIMIZED=1` starts with a maximized window by default. Set it to `0` to start
with the original 640-by-480 window. Press F11 to enter or leave borderless fullscreen at the
current monitor's resolution. Leaving fullscreen restores the previous window size and
maximized state. Switching releases the mouse cursor; click the game to capture it again.
Existing INI files gain missing widescreen and window settings on the next launch.

Other existing `PS2X_` and `SOTC_` settings can also be placed under `[Settings]` using their full
names. An environment variable with the same name takes precedence, so existing test scripts and
command-line overrides keep working. Escape no longer closes the window; use the window's close
button to exit. Enter remains the Start button.

Audio starts automatically with the game. The original sound driver supplies music, dialogue, and
effects from the disc image to a 48 kHz stereo stream. The game's sound options control voice and
master volumes. Reverb, hardware volume sweeps, noise, pitch modulation, and AutoDMA PCM input are
not implemented yet; normal ADPCM voices and the game's double-buffered ADPCM streams are supported.

Set `PS2X_AUDIO_MUTE=1` to silence host playback while keeping the emulated sound driver and mixer
running. This is useful for performance tests. Set it to `0` or remove it to restore normal playback
on the next launch. Audio dumps still contain the mixed sound when playback is muted.

`PS2X_AUDIO_STATS=1` reports production and playback rates, queued audio, missing frames,
underruns, and dropped frames every two seconds. These statistics include the host playback
queue, so they can reveal gaps that are absent from the pre-queue WAV dump.

For diagnostics, set `PS2X_AUDIO_DUMP` to a writable `.wav` path to record the mixed output before
the host playback queue. Close the game normally to finalize the WAV header. `Tools/audio_check.py`
records a scripted boot and reports the sample rate, channels, peak, and number of nonzero samples.
