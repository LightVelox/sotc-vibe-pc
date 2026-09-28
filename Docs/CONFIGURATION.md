# Configuration

The port creates `sotc.ini` beside `sotc.exe` on first launch. With `run.bat`, the file is
`build/port/bin/sotc.ini`. Edit it while the game is closed; settings take effect on the next launch.

```ini
[Settings]
PS2X_MEMCARD=1
PS2X_INVERT_RIGHT_STICK=xy
PS2X_GS_GPU=1
PS2X_GS_THREAD=1
PS2X_MTVU=1
PS2X_VU1_RECOMP=1
```

Set `PS2X_MEMCARD=0` to run without a memory card. The slot 1 card image is stored at
`memcards/Mcd001.ps2` beside the executable. `PS2X_INVERT_RIGHT_STICK` accepts `0`, `x`, `y`, or
`xy`. The other listed switches accept `0` or `1`.

Other existing `PS2X_` and `SOTC_` settings can also be placed under `[Settings]` using their full
names. An environment variable with the same name takes precedence, so existing test scripts and
command-line overrides keep working. Escape no longer closes the window; use the window's close
button to exit. Enter remains the Start button.
