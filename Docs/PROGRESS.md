# Progress

Last updated: 2026-09-23. Target: SCES-53326 v1.00 (see `GAME_BUILD.md`).

## Working

* **Build identification**: disc, boot ELF, modules and IRX files fingerprinted (SHA-256); Redump
  match confirmed via PCSX2's bundled database. Tooling and the executable refuse other builds.
* **XFF module format** reverse-engineered (`Docs/XFF_FORMAT.md`); our relocation of KERNEL, MANAGER
  and GAMECORE is byte-identical to PCSX2 guest memory except at 707 import sites re-patched by the
  loader at run time (all accounted for).
* **Function discovery**: 9,201 functions (6,813 named from the game's own symbol tables, the rest
  from relocations, call targets and function-boundary pointers). Database: `Docs/ADDRESS_MAP.csv`.
* **Static recompilation** of the boot ELF + all three modules: 0 decode failures, 0 unhandled
  instructions, 26,981 run-time relocation sites verified covered (`Tools/verify_reloc_sites.py`).
* **Native executable** (`build/port/bin/sotc.exe`): verifies the user's disc, loads the boot ELF,
  and executes recompiled code from the ELF entry point (`0x00100010`) on the runtime's EE scheduler.
* **ISO-backed disc access** in the runtime: `cdrom0:` opens, `sceCdSearchFile` (real LBNs), raw
  sector reads, and IOP module loading all read the user's image directly (no extraction).
* **EE FPU / VU0 macro semantics** (runtime + recompiler): saturation instead of Inf/NaN, EE divide
  by zero, `sqrt(|x|)`, `VRSQRT = fs/sqrt(|ft|)`, `CVT.W.S` saturation; host MXCSR round-toward-zero +
  FTZ/DAZ on the game thread.
* **Diagnostics**: categorized logs (`SOTC_TRACE`), native stack watchdog for hangs
  (`SOTC_WATCHDOG=<s>`), guest call tracer (`SOTC_TRACE_CALLS=<hex,...>`), native RAM dump at a guest
  function entry (`SOTC_DUMP_RAM_AT=<hex>:<file>`), missing guest functions stop the run.
* **PCSX2 oracle**: private PCSX2 copy with EE/IOP console + PINE; `Tools/pine.py` dumps guest RAM and
  can park the EE at an address (`trap`); `Tools/compare_ram.py` diffs dumps per region.

## Partially working

* Boot: libkernel `_InitSys` now completes its syscall-table discovery (see discoveries). Next run
  pending on the rebuilt executable.

## Known blockers

| Area | Blocker | Plan |
|---|---|---|
| Audio | `sg2iop_driver` (game IOP sound driver) imports LIBSD functions (ordinals 4–26); LIBSD programs SPU2 registers. The runtime has no IOP-side SPU2 and only an EE-side LIBSD RPC HLE. | SPU2 register model on the IOP side (or LIBSD import-library HLE) feeding a host mixer |
| Pad | libpad2/libdbc → `DS1O_D`/`DBCMAN` over SIO2; runtime fakes `sio2man/sio2d/dbcman` loads, DBCMAN HLE only answers version queries | DBCMAN/DS1O HLE = `VirtualDualShock2` boundary fed by a PC input backend |
| Memory card | libmc2 → `MC2_D` over SIO2/DBCMAN | same boundary as pad; host-file backed card |
| FMV | FFmpeg disabled | decide decoder strategy |

## Temporary hacks

| What | Where | Why | Exit criterion |
|---|---|---|---|
| FFmpeg disabled (MPEG → stub frames) | `CMakeLists.txt` `PS2X_ENABLE_FFMPEG OFF` | avoid downloading third-party prebuilt binaries during bring-up | decide FMV strategy |
| HLE bindings for SIF/file/CD/DECI2/TTY/MPEG/IPU | `Port/recomp/sotc.toml` | runtime IOP bridge is API-level | revisit per subsystem once compared with PCSX2 |
| Module load addresses measured, not derived | `Port/profile/SCES-53326_v1.00.json` | taken from PCSX2; checked at run time by `module_guard` | guard passes when the original loader runs natively |

No function is stubbed with `return 0`-style placeholders.

## Reverse-engineering discoveries

* The boot ELF is a dynamic loader (`ld:` messages, build Dec 1 2005) for `xff2` modules; it exports
  its own symbols through `KERNEL.XFF` (`SHN_ABS`).
* Game code keeps global symbol names: e.g. `st_mainloop`, `titlemenu`,
  `CAM_get_update_yaw_by_view_cut_avoid_cntrl`, `nico_horseRStart`. Static functions are unnamed.
* Imports that cannot be resolved are bound to KERNEL `0x001B28F8` (undefined-call trap); data modules
  from `NICO.DAT` later define symbols such as `_AnimObjDef` and code is re-patched.
* `.bss` of later modules is placed into memory freed from earlier modules' relocation tables.
* PCSX2 GameDB: EE rounding must be Chop for this game.
* `0x8010F300` (`_kDebugException`) is the libkernel debug handler installed for TLB exceptions; the
  game does not rely on custom TLB mappings.
* Boot ELF `crt0`: stack `0x01FE0000`+`0x20000`, `_gp = 0x0013E9F0`, heap `0x00164FE8`+`0x40000`,
  then `_InitSys`, `FlushCache`, `main (0x001039C8)` → `InitDisp`, `InitException`,
  `LoaderSysInit*`, `loaderLoop (0x00103848)` → `execProgWithThread`.
* libkernel `_InitSys` (`0x0010F5C8`) installs two EE routines as syscalls (`SetSyscall(0x83,
  0x0010F540)` word search, `SetSyscall(0x5A, 0x0010F508)` word copy) and uses them to locate the
  kernel syscall table (`slot83 - 0x20C == slot5A - 0x168`), storing the base at `0x0012F8D8`
  (`0x80014F40` with BIOS SCPH-70012 in PCSX2). Those handlers are referenced only from a data table
  at `0x0012F8E0`.
* The archive layer is `sheetFs*`/`sheetMgr*`/`sheetFile*` (MANAGER), on KERNEL
  `iosLoaderFOpen`/`iosFileRead`/`IosCdvdManagerSimulation`.
* VU0 macro instructions are the game's main source of square roots (870 `VRSQRT`, 363 `VSQRT`);
  FPU `div.s` appears 1,359 times, `cvt.w.s` 1,054 times.
* IOP import map (from IRX import tables, `Tools/irx_imports.py`): `sg2iop_driver` → libsd, sifcmd,
  sysclib, thbase; `DS1O_D` → sio2man, sio2d, dbcman, thevent, vblank; `MC2_D` → sio2man, sio2d,
  dbcman, secrman, cdvdman.

## Next priorities

1. Run the rebuilt executable; reach `loaderLoop` → KERNEL entry with `module_guard` verification.
2. Record `checkpoint_boot` (native vs PCSX2 RAM at KERNEL entry via `compare_ram.py`).
3. Resolve blockers toward MANAGER/GAMECORE entry and the first rendered frame.
4. Audit the 720 "unresolved JR/JALR" warnings: most are indirect calls through callbacks (handled by resume entries); feed exact switch tables from `.rodata` relocations where they are true switches.
5. Make `j` tail-jumps dispatch through the function table (upgrade-path completeness).
6. IOP audio path (SPU2/LIBSD) and DBCMAN pad HLE.
