# Progress

Last updated: 2026-09-23. Target: SCES-53326 v1.00 (see `GAME_BUILD.md`).

## Working

* **Build identification**: disc, boot ELF, modules and IRX files fingerprinted (SHA-256); Redump
  match confirmed via PCSX2's bundled database. Tooling and the executable refuse other builds.
* **XFF module format** reverse-engineered (`Docs/XFF_FORMAT.md`); our relocation of KERNEL, MANAGER
  and GAMECORE is byte-identical to PCSX2 guest memory except at 707 import sites re-patched by the
  loader at run time (all accounted for).
* **Function discovery**: 9,190 functions (6,813 named from the game's own symbol tables, the rest
  from relocations/call targets). Database: `Docs/ADDRESS_MAP.csv`.
* **Static recompilation** of the boot ELF + all three modules with PS2Recomp: 0 decode failures,
  0 unhandled instructions, 26,981 run-time relocation sites verified covered.
* **ISO-backed disc access** in the runtime: `cdrom0:` opens, `sceCdSearchFile` (real LBNs), raw
  sector reads, and IOP module loading all read the user's image directly (no extraction).
* **PCSX2 oracle**: private PCSX2 copy with EE/IOP console + PINE; `Tools/pine.py` dumps guest RAM.

## Partially working

* Native executable build (`build.bat`): compiles; first execution results pending.

## Known blockers

* None identified yet beyond "first run pending".

## Temporary hacks

| What | Where | Why | Exit criterion |
|---|---|---|---|
| FFmpeg disabled (MPEG → stub frames) | `CMakeLists.txt` `PS2X_ENABLE_FFMPEG OFF` | avoid downloading third-party prebuilt binaries during bring-up | decide FMV strategy (build FFmpeg from source or native PSS decoder) |
| HLE bindings for SIF/file/CD/DECI2/TTY/MPEG/IPU | `Port/recomp/sotc.toml` | runtime IOP bridge is API-level | revisit per subsystem once behaviour is compared with PCSX2 |
| Module load addresses measured, not derived | `Port/profile/SCES-53326_v1.00.json` | taken from PCSX2; checked at run time by `module_guard` | derive by running the original loader natively (guard passes) |

No function is stubbed with `return 0`-style placeholders.

## Reverse-engineering discoveries

* The boot ELF is a dynamic loader (`ld:` messages, build Dec 1 2005) for `xff2` modules; it exports
  its own symbols through `KERNEL.XFF` (`SHN_ABS`).
* Game code keeps global symbol names: e.g. `st_mainloop`, `titlemenu`, `CAM_get_update_yaw_by_view_cut_avoid_cntrl`,
  `nico_horseRStart`. Static functions are unnamed.
* Imports that cannot be resolved are bound to KERNEL `0x001B28F8` (undefined-call trap); data modules
  from `NICO.DAT` later define symbols such as `_AnimObjDef` and code is re-patched.
* `.bss` of later modules is placed into memory freed from earlier modules' relocation tables.
* PCSX2 GameDB: EE rounding must be Chop for this game.
* The game installs a custom TLB refill handler at `0x8010F300`.
* Boot ELF `crt0`: stack `0x01FE0000`+`0x20000`, `_gp = 0x0013E9F0`, heap `0x00164FE8`+`0x40000`,
  then `_InitSys`, `FlushCache`, `main (0x001039C8)` → `InitDisp`, `InitException`,
  `LoaderSysInit*`, `loaderLoop (0x00103848)`.

## Next priorities

1. First native run: reach `loaderLoop`, KERNEL module entry with `module_guard` verification.
2. Record `checkpoint_boot` (PCSX2 vs native: PC, registers, module hashes).
3. Resolve blockers toward MANAGER/GAMECORE entry and the first rendered frame.
4. Feed exact switch jump tables (from `.rodata` relocations) to the recompiler to remove 710
   fallback promotions.
5. Make `j` tail-jumps dispatch through the function table (upgrade path completeness).
6. FPU overflow clamping semantics (EE has no Inf/NaN).
