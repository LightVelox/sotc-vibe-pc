![Shadow of the Colossus — native PC port (fully vibe-coded)](social-preview-1280x640.jpg)

# Shadow of the Colossus — native PC port (fully vibe-coded)

A playable, unofficial native PC port of **Shadow of the Colossus** (PS2, PAL, SCES-53326 v1.00) for **Windows and
Linux**. The game's PlayStation 2 code is statically recompiled to C++ with
[PS2Recomp](https://github.com/ran-j/PS2Recomp) and built into a native executable for each system: there is no
emulator to configure, you build it once from your own disc and play.

This repository is a fork of [LightVelox/sotc-vibe-pc](https://github.com/LightVelox/sotc-vibe-pc), the original
Windows port. The fork adds the native Linux build and speeds up the GPU renderer used on graphics drivers without
fragment-shader interlock, such as AMD's on Linux.

**This project is fully vibe-coded.** Every line of code, every tool and every document in this repository was
written by AI coding agents (Anthropic's Claude through Claude Code, and OpenAI's ChatGPT/Codex). A human directed
the work, played the builds and reported what was wrong, but did not write the code by hand. Treat it accordingly:
it works surprisingly well, and some of it is strange.

> **No game data is included.** This repository contains no disc image, no game files, no BIOS, no emulator and no
> code generated from the game. You need your own legally obtained copy of the game. The tools only accept the
> exact SCES-53326 v1.00 disc and check the hashes of its game code before doing anything.
>
> This is a fan project. It is not affiliated with or endorsed by Sony Interactive Entertainment, Team Ico or
> Japan Studio. *Shadow of the Colossus* is a trademark of its respective owners.

## Platforms

| | Windows | Linux |
|---|---|---|
| System | Windows 10 or 11, x64 | x86-64 distribution; X11, or Wayland through XWayland |
| CPU | AVX2 (Intel Haswell / AMD Excavator or newer) | Same |
| GPU | OpenGL 4.6 | OpenGL 4.6 (Mesa or proprietary driver) |
| Compiler | MSVC (Visual Studio 2022 or newer) | GCC or Clang |
| Get it | Prebuilt package or build from source | Build from source |
| One-step setup | `setup.bat` | `./setup.sh` |
| Tested on | Ryzen 5 5500, RTX 3060 | Ubuntu 25.10, Ryzen 9 6900HX, Radeon 680M (Mesa radeonsi) |

## What works

- Boot, language/video selection, title screen, intro cutscene, gameplay, riding Agro, climbing and fighting.
- All 16 colossus fights pass the automated scenario harness (warp, fight, kill, death cutscene, save prompt), and
  the early colossi have been played through by hand.
- Memory card saves (stored as a PCSX2-compatible card image), plus instant save states on F5 / F9.
- 60 Hz (NTSC) mode, widescreen with proportional UI, maximized/fullscreen window (F11).
- Mouse camera, keyboard bindings and gamepad input.
- Sound and music through an emulated IOP and SPU2.
- Performance: on a Ryzen 5 5500 with an RTX 3060 (Windows), heavy scenes such as the lake, the canyon and the
  Gaius fight run at a steady 60 fps. On a Radeon 680M integrated GPU (Linux), riding through the open field runs
  at about 45 fps. The original console often ran at 20–30 fps.

## Known issues

- Occasionally a single black frame flashes on screen.
- The first time a new kind of surface appears, a few frames can be slower while its GPU shader compiles in the
  background. Compiled shaders are cached on disk, so this happens once per PC.
- Audio is functional but not a perfect SPU2 reproduction.
- Only the PAL v1.00 disc is supported.
- On GPUs without `GL_ARB_fragment_shader_interlock` (for example AMD on Windows OpenGL, and AMD with Mesa's
  radeonsi driver on Linux), the renderer falls back to the slower compute-shader path automatically.
- Linux support is newer than Windows: it has been played on fewer machines, the scenario harness has only been
  run on Windows, and there is no prebuilt Linux package yet.

## How it works

- **EE (main CPU):** the game's MIPS R5900 code (boot ELF plus the `KERNEL`, `MANAGER` and `GAMECORE` XFF modules,
  relocated statically) is translated to C++ ahead of time and compiled with MSVC on Windows or GCC/Clang on Linux.
  A small runtime provides the PS2 kernel, threads, interrupts, timers and DMA.
- **VU1 (vector unit):** the game's VU1 microprograms are recompiled to C++ as well and run on their own thread,
  with an interpreter fallback.
- **IOP (I/O processor):** the game's IOP modules run in an emulated R3000 with high-level replacements where
  useful (CD/DVD, memory card, pad, sound).
- **GS (graphics):** an OpenGL 4.6 renderer (WGL contexts on Windows, EGL on Linux). GS primitives are rasterized by
  the GPU, with the exact PS2 coverage rules, blending and depth tests in specialized fragment shaders ordered by
  fragment-shader interlock. Where the driver lacks interlock, a compute-shader rasterizer takes over; a CPU
  reference renderer is kept for verification.
- PCSX2 was used during development as a reference ("oracle") to compare RAM, GS output and timing. It is not
  part of this repository or the build.

More detail: [Docs/ARCHITECTURE.md](Docs/ARCHITECTURE.md), [Docs/PROGRESS.md](Docs/PROGRESS.md) (the full
development log), [Docs/GAME_BUILD.md](Docs/GAME_BUILD.md), [Docs/CONFIGURATION.md](Docs/CONFIGURATION.md),
[Docs/PERFORMANCE.md](Docs/PERFORMANCE.md) and [Docs/TESTING.md](Docs/TESTING.md).

## Prebuilt Windows packages

Experimental prebuilt Windows ZIPs are published on the original project's
[GitHub Releases](https://github.com/LightVelox/sotc-vibe-pc/releases). Extract one, put your supported ISO in its
`Game` folder, and run `Play.bat`. You can also drag the ISO onto `Play.bat` to keep it in another folder.
Prebuilt packages include the C++ runtime DLLs and do not require Visual Studio, Python, CMake, Ninja, or local
compilation. To create a package from a finished build, see [Docs/RELEASING.md](Docs/RELEASING.md).

On Linux, build from source as described below.

## Building from source

### Requirements

Both systems:

- Python 3.10 or newer.
- A CPU with AVX2 and a GPU with OpenGL 4.6.
- Your own dump of **Shadow of the Colossus (Europe, Australia), SCES-53326, version 1.00**, as a 2048-byte-sector
  ISO (`SHA-256 068925770645266189005f09c9a9a1fee25726bfc93c89b12f832f3673664ac4`).
- Disk space and patience: the generated game code is about 9,000 C++ files and a full build takes around an hour.

Windows:

- MSVC x64 build tools from Visual Studio 2022 or newer (including Visual Studio 2026), or the standalone
  [Build Tools](https://visualstudio.microsoft.com/visual-cpp-build-tools/). The full Visual Studio IDE is optional.
  In Visual Studio Installer, select **Desktop development with C++**, a **Windows 10 or 11 SDK**, and
  **C++ CMake tools for Windows**. CMake and Ninja can also be installed separately on `PATH`.

Linux:

- GCC or Clang, CMake 3.24 or newer, Ninja, pkg-config and the X11/OpenGL development headers. On Debian or Ubuntu:

  ```sh
  sudo apt install build-essential cmake ninja-build pkg-config \
      libgl-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
  ```

  `setup.sh` prints the Fedora and Arch equivalents if anything is missing.

### One-step setup

```sh
git clone --recursive <this repository>
cd <repository>
setup.bat      # Windows
./setup.sh     # Linux
```

Setup checks the Git submodules, Python and the C++ build tools, finds your disc image in `Game/` and checks its
size, and tells you exactly what to fix if something is missing. It then builds the recompiler, generates the game
code (verifying the SHA-256 of the boot ELF and game modules), builds the port and offers to start the game.
Running it again is safe: finished steps are skipped or re-checked quickly.

On Windows, setup discovers Visual Studio and standalone Build Tools installations using Microsoft's `vswhere`,
including custom install locations; set `VSROOT` to an installation directory to select one explicitly. On Linux,
the build runs about one compiler job per 2 GB of RAM, since each job on the generated code can take 1–2 GB; set
`SOTC_JOBS` to override it.

### Step by step

To do the same steps by hand, first put your disc image in `Game/`. The folder is part of the repository and
contains a `PLACE-YOUR-ISO-HERE.txt` placeholder; any `*.iso` of the right size is found automatically, or set
`SOTC_ISO`. Then:

| Step | Windows | Linux |
|---|---|---|
| 1. Configure | `configure.bat` | `./configure.sh` |
| 2. Build the recompiler | `build.bat ps2_recomp` | `./build.sh ps2_recomp` |
| 3. Generate the game code from your disc | `Tools\generate.bat` | `Tools/generate.sh` |
| 4. Configure again and build the port | `configure.bat`, then `build.bat` | `./configure.sh`, then `./build.sh` |
| 5. Play | `run.bat` | `./run.sh` |

Step 3 verifies the disc's game code, relocates the modules and writes `Port/generated/` (git-ignored, never commit
it). The executable is `build/port/bin/sotc.exe` on Windows and `build/port/bin/sotc` on Linux. On first launch it
writes `sotc.ini` next to itself.

Recompiled VU1 programs are optional. Without them VU1 code runs in the interpreter, which is slower. Producing
them means capturing the game's VU1 microcode at run time (`PS2X_VU1_CAPTURE`) and running the `vu1_recomp`
tool; see the VU1 sections of [Docs/PROGRESS.md](Docs/PROGRESS.md).

## Controls

| Action | Default |
|---|---|
| Move (left stick) | W A S D |
| Camera (right stick) | Mouse |
| Cross (jump) | F |
| Circle (draw/raise weapon, dismount) | Right mouse button |
| Square (attack, shoot) | Left mouse button |
| Triangle (whistle, mount) | Space |
| R1 (grab) | Left Shift |
| L1 | Q |
| L2 / R2 | Left Ctrl / Right Shift |
| D-pad (switch weapon) | Arrow keys |
| Start / Select | Enter / Tab |
| L3 / R3 | H / G |
| Save state / load state | F5 / F9 |
| Performance overlay | F3 |
| Fullscreen | F11 |

A gamepad also works. Bindings, mouse sensitivity, stick inversion, widescreen, video mode and the renderer
switches are all in `sotc.ini`; see [Docs/CONFIGURATION.md](Docs/CONFIGURATION.md).

## Repository layout

| Path | Contents |
|---|---|
| `Port/` | The game-specific port: entry point, HLE hooks, mouse camera, widescreen, debug tools, recompiler config |
| `External/PS2Recomp/` | Fork of PS2Recomp (submodule): recompiler, runtime, IOP emulator, GS renderers, tests |
| `Tools/` | Python tooling: code generation, module linking, disassembly, PCSX2 comparison, test harnesses |
| `Docs/` | Architecture, formats, configuration, performance notes and the development log |
| `Analysis/` | Address map and function notes derived from the game's symbol tables |
| `*.bat`, `*.sh` | Setup, configure, build and run scripts for Windows and Linux |

Git-ignored and never committed: `Game/*.iso` (your disc image; the `Game/` folder itself is committed
with a placeholder telling you what to drop in), `Emulator/`, `Tools/pcsx2-oracle/`, `Port/generated/`,
`Analysis/extracted/`, `Analysis/oracle/` and `build/`.

## License

GPL-3.0, the same as PS2Recomp, which this port builds on. See [LICENSE](LICENSE). The license covers the code in
this repository only. It grants nothing for the game itself.

## Credits

- [sotc-vibe-pc](https://github.com/LightVelox/sotc-vibe-pc) by LightVelox: the original Windows port this fork
  builds on.
- [PS2Recomp](https://github.com/ran-j/PS2Recomp) by ran-j and contributors: the static recompiler and runtime
  this port started from.
- [PCSX2](https://pcsx2.net/), used as the reference emulator while developing.
- [raylib](https://www.raylib.com/), [ELFIO](https://github.com/serge1/ELFIO), [rabbitizer](https://github.com/Decompollaborate/rabbitizer),
  [fmt](https://github.com/fmtlib/fmt) and [toml11](https://github.com/ToruNiina/toml11).
- Team Ico and Japan Studio, for the game.
