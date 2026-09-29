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
