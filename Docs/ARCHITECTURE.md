# Architecture

Goal: a native PC executable that runs the **original** game code of Shadow of the Colossus
(SCES-53326 v1.00) through static recompilation, with a host runtime replacing the PS2 hardware, and
clean seams that let individual functions and subsystems migrate from "recompiled original" to
"understood/decompiled" to "clean native" without rewriting unrelated code.

This is not an emulator frontend: there is no interpreter for EE code, no PCSX2 at run time. The
only interpreted code is IOP (R3000A) driver code and VU microprograms, both confined to the
runtime's hardware layer and replaceable.

## Repository layout

```
Game/        user's own disc image (not committed)
Emulator/    reference PCSX2 install (not committed, never modified)
External/PS2Recomp   submodule: recompiler + runtime, local branch `sotc-port` with generic fixes
Tools/       Python tooling (ISO reader, XFF parser/linker, PCSX2 PINE client, generators, verifiers)
Tools/pcsx2-oracle   private copy of PCSX2 with logging + PINE enabled (not committed)
Port/        the native game: host program, game layer, recompiler config, build profile
Port/generated       recompiled C++ (generated from the user's disc; not committed)
Analysis/    extracted files, oracle dumps (not committed), function database
Docs/        documentation
```

## Build pipeline

```
user's ISO ──► Tools/sotc_link.py ──► build/link/sotc_linked.elf      (synthetic ELF, all code at run-time addresses)
   │             (verifies hashes,    build/link/sotc_functions.csv    (function map: 9,190 functions)
   │              links modules,      build/link/sotc_runtime_relocs.csv (26,981 run-time patched sites)
   │              discovers funcs)    Port/generated/sotc_layout_generated.h
   │
   └──────────► ps2_recomp (PS2Recomp, patched) + Port/recomp/sotc.toml ──► Port/generated/*.cpp
                                                                          │
                              CMake (root CMakeLists.txt) ◄────────────────┘
                                   │
                                   ▼
                              build/port/bin/sotc.exe  ──runs──►  reads the user's ISO directly
```

A clean checkout plus the user's disc image regenerates everything (`Tools/generate.bat`), and no
game code or data is committed.

### Why a synthetic ELF

The boot ELF is only a loader. The game lives in three `xff2` modules that the loader relocates at run
time (`Docs/XFF_FORMAT.md`). `sotc_link.py` reproduces that relocation at the addresses the original
loader chooses (measured in PCSX2, verified byte-for-byte), and emits one ELF containing the boot ELF
plus all module sections at their run-time addresses, with symbols. PS2Recomp then recompiles it like
any other executable.

Addresses are **not** assumed at run time: the recompiled original loader loads the modules itself,
and the port verifies at each module's entry that the code in guest memory matches the recompiled
image (`Port/src/sotc/module_guard.cpp`). If a future change altered the loader's allocation pattern,
the port stops with a precise error instead of executing mismatched code.

### Run-time relocation sites

The loader patches import sites in MANAGER/GAMECORE when data modules are loaded later. The
recompiler was extended (`general.runtime_relocations`) so that those instructions take their
immediate (`lui/addiu/lw/...`) or jump target (`j/jal`) from the live instruction word in guest RAM.
`Tools/verify_reloc_sites.py` proves every site is covered after each regeneration.

## Run-time architecture

```
                         ┌─────────────────────────── sotc.exe ───────────────────────────┐
                         │                                                                 │
 original game code ───► │  Port/generated/*.cpp   (recompiled EE code: boot ELF +        │
 (recompiled, 9,190 fn)  │                          KERNEL/MANAGER/GAMECORE)              │
                         │        │ jal/jalr → runtime->dispatchGuestBranch()             │
                         │        ▼                                                        │
                         │  function table  g_ps2RecompiledFunctionTable[addr]            │
                         │        ▲  replaceFunction()                                     │
 game layer ───────────► │  Port/src/sotc/function_hooks  (observe / replace / native)    │
                         │  Port/src/sotc/module_guard    (module verification, FPU mode) │
                         │  Port/src/sotc/game_disc       (disc identity, boot ELF cache) │
                         │                                                                 │
 platform (PS2Recomp) ─► │  ps2xRuntime: EE kernel syscalls, EE scheduler, memory/MMIO,   │
                         │    DMA, VIF1, VU0/VU1 interpreter, GS, pad, audio, VFS         │
                         │  ps2xIOP: IRX execution (R3000A) + HLE services                 │
                         │  PS2CdImage: ISO9660 disc image (cdrom0:, sceCdRead, IOP)       │
                         └─────────────────────────────────────────────────────────────────┘
```

### Function ownership and the upgrade path

Every guest function address maps to one implementation in the dense function table. Calls (`jal`,
`jalr`) always go through the runtime dispatcher, so replacing a table slot redirects every caller.
`sotc::FunctionHooks` is the single place the game layer changes a slot, and records why:

| Status | Meaning |
|---|---|
| `recomp` | generated code from the original binary (default) |
| `recomp+observed` | original code, with an entry observer (logging, verification, FPU setup) |
| `native` | readable C++ replacement, verified against the original/PCSX2 |
| `TEMPORARY-STUB` | diagnostic only; listed at start-up and in `Docs/PROGRESS.md` |

```cpp
FunctionHooks::instance().replace(0x0134AC08, "Camera_Update", &Native_Camera_Update,
                                  ReplacementStatus::Native, "verified vs PCSX2 checkpoint_world");
```

Known gap: `j` tail-jumps between recompiled functions are emitted as direct C++ calls and bypass the
table. Before the first native replacement of a tail-jump target lands, the recompiler must be told
to dispatch those through the table as well (tracked in `Docs/PROGRESS.md`).

`Analysis/ADDRESS_MAP.csv` (copied to `Docs/ADDRESS_MAP.csv`) records, for every function, the
address, generated name, provisional name, where the name came from (symbol table vs. discovered),
confidence and replacement status. Names from the game's own symbol tables are marked `high`;
`sub_XXXXXXXX` entries have no name yet.

### HLE boundary

PS2Recomp can bind guest functions to runtime handlers by name. Upstream does this automatically; the
port disables that (`auto_stub_by_name = false`) and lists every binding explicitly in
`Port/recomp/sotc.toml`, so each deviation from original code is deliberate:

* **SIF / IOP RPC, file I/O, CD/DVD, DECI2/TTY:** the runtime's IOP bridge works at the SCE API level
  (there is no hardware-level SIF DMA), so these libraries are bound to runtime implementations.
* **MPEG/IPU:** bound to runtime (FFmpeg-backed decoder, currently disabled → stub frames).
* **Kept as original code:** newlib (`malloc` family — heap layout decides module addresses; `rand` —
  gameplay randomness; string and math routines), GS/DMA/VIF packet libraries, all game code.

### Floating point

The EE FPU is not IEEE: it truncates, flushes denormals and has no Inf/NaN. PCSX2's database forces
"Chop" rounding for this game (Wander drifts otherwise). The game thread therefore runs with MXCSR set
to round-toward-zero + FTZ + DAZ, set by an entry observer on the boot entry point, and the game code
is compiled with `/fp:precise` (upstream's Release `/fp:fast` would reorder and contract float math).
Clamping of overflowed results to ±FLT_MAX is not yet implemented (tracked).

### Rendering, audio, input

Currently provided by the PS2Recomp runtime (GS emulation to a raylib window; audio backend; pad via
raylib). The planned structure, not yet implemented:

```
Game logic (recompiled) ─► GIF/VIF/VU packets ─► GS command stream ─► Renderer interface ─► backend
Input backend (SDL/XInput) ─► PCInputBackend ─► VirtualDualShock2 ─► pad driver replies (IOP)
SPU2 / sg2iop_driver ─► audio mixer interface ─► host audio
```

Nothing here is enhanced: resolution, frame rate, textures and draw distance stay original until the
native baseline is proven against PCSX2 checkpoints.

What the game actually requires on the IOP side (from IRX import tables, `Tools/irx_imports.py`):

* **Audio.** The game's own driver `SG2IOPM1.IRX` (`sg2iop_driver`) imports LIBSD functions directly
  (ordinals 4–26) and is driven from EE-side `Sg2*` code in KERNEL. LIBSD programs SPU2 registers and
  DMA channels 4/7. The runtime currently offers only a coarse VAG-sample player and an EE-side LIBSD
  RPC service, so faithful audio needs an SPU2 model behind the IOP (register level, or LIBSD-import
  HLE) feeding a host mixer. Candidate: an existing GPL-compatible SPU2 implementation behind a
  small interface, so it can later be swapped for a native mixer.
* **Pad and memory card.** libpad2/libdbc (EE) talk to `DBCMAN`, which drives `DS1O_D` (DualShock)
  and `MC2_D` (memory card) over `SIO2MAN`/`SIO2D`. The runtime fakes those module loads; the
  `VirtualDualShock2` and memory-card backends will be implemented at the DBCMAN service boundary.

## PCSX2 as the behavioural oracle

`Tools/pcsx2-oracle` is a copy of the supplied PCSX2 with EE/IOP console logging and PINE enabled.
`Tools/pine.py` reads guest memory (`info`, `read32`, `dump`). Every address/assumption in the port is
derived from, or checked against, this oracle (e.g. the module layout above).
