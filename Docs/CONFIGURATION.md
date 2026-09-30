# Configuration

The port creates `sotc.ini` beside `sotc.exe` on first launch. With `run.bat`, the file is
`build/port/bin/sotc.ini`. Edit it while the game is closed; settings take effect on the next launch.

```ini
[Settings]
PS2X_MEMCARD=1
PS2X_INVERT_RIGHT_STICK=xy
PS2X_MOUSE_SENSITIVITY=1.0
PS2X_MOUSE_VERTICAL_SENSITIVITY=1.5
PS2X_GS_GPU=1
PS2X_GS_THREAD=1
PS2X_MTVU=1
PS2X_VU1_RECOMP=1
SOTC_VIDEO_MODE=NTSC
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
relative to horizontal movement; its default of `1.5` compensates for the game's slower pitch response.

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
