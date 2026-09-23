# Game build identification

Everything in this project targets exactly one build. Addresses, hashes and module layouts below are
valid **only** for this build; the tooling refuses other images (`Tools/sotc_link.py` and the native
executable both verify SHA-256 hashes before doing anything).

## Disc image

| Property | Value |
|---|---|
| File supplied | `Game/SHADOW_COLOSSUS (PAL).iso` |
| Title (Redump) | Shadow of the Colossus (Europe, Australia) (En,Fr,De,Es,It) |
| Serial | **SCES-53326** |
| Region / video | PAL (Europe, Australia); `SYSTEM.CNF` `VMODE = PAL` |
| Version | **1.00** (`SYSTEM.CNF` `VER = 1.00`, Redump `version: '1.00'`) |
| Format | 2048-byte-sector ISO9660 (DVD-5 single layer), no UDF needed |
| Size | 4,635,918,336 bytes (2,263,632 sectors) |
| Volume creation date | 2005-12-14 20:55:04 |
| MD5 | `13871262c004856b175c0ea3ac65a430` (matches PCSX2's bundled Redump database entry) |
| SHA-1 | `34c3f1b4fbe6f9a340720cbc02cbe6947c817d73` |
| SHA-256 | `068925770645266189005f09c9a9a1fee25726bfc93c89b12f832f3673664ac4` |
| PCSX2 game CRC | `0F0C4A9C` (reported by PCSX2 2.8.2 on boot) |

PCSX2's GameIndex entry for SCES-53326 sets `eeRoundMode: 3` (Chop, "Prevents Wander from moving
when charging the floor"), i.e. EE FPU results must be truncated toward zero. The native port sets the
host SSE rounding mode to round-toward-zero with denormal flushing on the game thread
(`Port/src/sotc/module_guard.cpp`).

## `SYSTEM.CNF`

```
BOOT2 = cdrom0:\SCES_533.26;1
VER = 1.00
VMODE = PAL
```

## Root filesystem

| LBA | Size | Path | Role |
|---:|---:|---|---|
| 293 | 56 | `SYSTEM.CNF` | boot configuration |
| 294 | 229,888 | `SCES_533.26` | boot ELF: SCE runtime libraries + XFF dynamic loader |
| 407 | 275,345 | `IOPRP300.IMG` | IOP reboot/replacement image (ROMDIR) |
| 566 | 26 | `STARTUP.XFF` | text: `cdrom0:\KERNEL.XFF;1 0x400` (first module + argument) |
| 567 | 419,668 | `KERNEL.XFF` | xff2 module: game OS layer ("ios*"/"isys*"), loader front end, exports boot ELF symbols |
| 787 | 27,717 | `SG2IOPM1.IRX` | game sound driver for the IOP (`sg2iop_driver` 1.00) |
| 811 | 1,732,264 | `MANAGER.XFF` | xff2 module: engine (render, motion, collision, camera, VU microcode) |
| 1657 | 2,157,144 | `GAMECORE.XFF` | xff2 module: game logic (player, horse, colossi, scripts, menus) |
| 2762 | 1,073,741,824 | `NICO.DAT` | data archive part 1 (engine codename "NICO") |
| 527050 | 1,073,741,824 | `XAB.` | data archive part 2 |
| 1051338 | 1,073,741,824 | `XAC.` | data archive part 3 |
| 1575626 | 1,073,741,824 | `XAD.` | data archive part 4 |
| 2099914 | 314,316,264 | `XAE.` | data archive part 5 |
| — | — | `MODULES/`, `MODULES2/` | IOP driver modules (below) |

`NICO.DAT` + `XAB..XAE` are physically contiguous on disc (each part starts exactly where the previous
ends), forming one ~4.3 GB archive. Its header begins with a directory table (`stage`,
`stagetexseg_def`, ...). The format is not yet documented; the game can be served by raw sector reads
from the image, which the runtime supports, so no asset conversion is needed for bring-up.

## Hashes of important binaries (SHA-256)

| File | SHA-256 |
|---|---|
| `SCES_533.26` | `851f0c4f48b0b4bb61e89fb7e97d4e9b4ac4c2ea0ff143d1d95e18337ea8b57c` |
| `KERNEL.XFF` | `d83a2d4d7631ed740946849b1b4ba32620891fc41817bb16175fedb9f9cd6463` |
| `MANAGER.XFF` | `2de4d80eed7ff6c89b34b587ee1ced8792921e2a7c80d7e92a4fbb1481f1461a` |
| `GAMECORE.XFF` | `aa5a72b239b36da0bfc9f435bf31441057b4f4b8a01405b94b0e3b212fd242e0` |
| `STARTUP.XFF` | `661bef7e10930b3470defd778e3cb811c61070bea51d82d341ad7dc5151701aa` |
| `SYSTEM.CNF` | `ae6f63bb6476d90bd568724b1e25fea9fd145596f61c4a3de25d963c2918d893` |
| `IOPRP300.IMG` | `02d314464c17c715d7e809297b0aba184173a98616b1fafeac27fe33fc480c82` |
| `SG2IOPM1.IRX` | `35fe78aba8dad6ed5524b05e277f31654a62f486d8405dd60c71ad15d172eba7` |
| `MODULES/LIBSD.IRX` | `94915a3d9673e8a370e144ae493552d5e1f3956bb9ae5bd91e2da5f36c83ce7d` |
| `MODULES/SIO2MAN.IRX` | `565ec77e75f174cfddf72c41ccc446c1c07a3d5fb0a2ccfefad337cb8a7d0728` |
| `MODULES/DBCMAN.IRX` | `60bfea7d0877df3af370676666e5f1043b6e71723fab94cdff790c19e75e12e7` |
| `MODULES/SIO2D.IRX` | `b07339d0b6ef6021ebb2bd8dba1138f3159b2cdfbf1b29df799694107107d330` |
| `MODULES/MC2_D.IRX` | `e4c9df69322819807761baff5b3fd33a66054ac4cc76e24d6558f6f210b12a04` |
| `MODULES/DS1O_D.IRX` | `ba65aefc36ef49fc2c31b979aef628cf304561c421b817d3734aee066082937c` |
| `MODULES2/MCMAN.IRX` | `b4df4061f8fdc9c7cf06a329b96227ad6ca69d008df475c1fc4d3e8d06fe3fa4` |
| `MODULES2/MCSERV.IRX` | `5006b0dd650fa416d03a559bc8e3bae8a7ae95be81f3e4f3fd9bd524fa27e000` |

## Boot ELF `SCES_533.26`

* ELF32 little-endian, `EM_MIPS`, `ET_EXEC`, `e_flags = 0x20924001` (R5900, EABI64 `.mdebug.eabi64`).
* Entry point **`0x00100010`**.
* No symbol table (stripped). However `KERNEL.XFF` exports 855 `SHN_ABS` symbols that name functions
  and objects inside this ELF (780 sized functions, e.g. `strcpy = 0x125EF0`,
  `sceSifAddCmdHandler = 0x112500`, the loader's own `ResolveRelocation = 0x1000D0`). They cover 78%
  of `.text`; the rest is discovered from `jal` targets, pointer tables and `lui/addiu` pairs.
* Loader build stamp: `Initialize loader (Version: ... Dec  1 2005 21:08:53`.
* SCE library stamps present: libkernl 3000, libpkt 3000, libgraph 3000, libdma 3000, libpad2 3020,
  libvib 3000, libdbc 3020, libcdvd 3000, libmrpc 3000, libmc2 3020.

| Section | Address | Size | Notes |
|---|---|---|---|
| `.text` | `0x00100000` | `0x2EAEC` | SCE libs, newlib, XFF loader |
| `.text_nop` | `0x0012EAEC` | `0x8` | |
| `.reginfo` | `0x0012EAF4` | `0x18` | |
| `.data` | `0x0012EB80` | `0x4464` | |
| `.ctors` / `.dtors` | `0x00132FE4` / `0x00132FEC` | 8 / 8 | |
| `.eh_frame` | `0x00132FF8` | 4 | |
| `.rodata` | `0x00133000` | `0x3988` | |
| `.lit4` | `0x00136A00` | 4 | |
| `.data_nop` | `0x00136A04` | 8 | |
| `.sdata` | `0x00136A80` | `0x398` | `_gp = 0x0013E9F0` (from KERNEL.XFF ABS symbol) |
| `.sbss` | `0x00136E80` | `0xC` | |
| `.bss` | `0x00136F00` | `0x2E0E8` | |

Program headers: `PT_MIPS_REGINFO`, `PT_LOAD 0x100000 (0x2EB0C, RWX)`, `PT_LOAD 0x12EB80
(filesz 0x8298, memsz 0x36468, RW)`.

## Runtime-loaded code: xff2 modules

The game code proper is **not** in the ELF. `SCES_533.26` is a dynamic loader that reads
`STARTUP.XFF`, loads `KERNEL.XFF`, which then loads `MANAGER.XFF` and `GAMECORE.XFF`. Each is an
`xff2` relocatable module with full `.symtab`/`.strtab` (global function names retained, static
functions stripped) and MIPS REL relocations. See `Docs/XFF_FORMAT.md`.

Load addresses observed in PCSX2 at the title screen (cold boot) and verified byte-exact against our
own relocation of the files:

| Module | Image base | `.text` | `.text` size | `.bss` | Entry | Functions (named) |
|---|---|---|---|---|---|---|
| KERNEL | `0x001A5400` | `0x001A7530` | `0x34CD0` | `0x001EFA00` | `0x001AE038` | 953 global |
| MANAGER | `0x0116BC40` | `0x01170830` | `0xFE3E4` | `0x0131E040` | `0x01178438` | 2370 global |
| GAMECORE | `0x013442C0` | `0x0134AC08` | `0x12C288` | `0x012E5540` | `0x013554D0` | 2706 global |

MANAGER additionally contains `.vutext` (`0x0126EC20`, `0x1AE70` bytes of VU microcode), a
`.DVP.ovlytab` and 60 `.DVP.overlay` VU1 microprogram overlays.

Data modules in the same `xff2` format are loaded later from `NICO.DAT` (e.g. definitions of
`_AnimObjDef`, `ControlMode_*`, `AdpcmSeq_*`) and **the loader re-patches code in MANAGER/GAMECORE
when they define previously unresolved imports**. At the title screen 707 code words differ from the
link-time image, all of them at known import-relocation sites.

## IOP modules

| File | `.iopmod` name | Version |
|---|---|---|
| `MODULES/SIO2MAN.IRX` | sio2man | 3.01 |
| `MODULES/DBCMAN.IRX` | Dbc_Manager | 3.10 |
| `MODULES/SIO2D.IRX` | sio2d | 3.03 |
| `MODULES/DS1O_D.IRX` | ds1o_d | 3.10 |
| `MODULES/MC2_D.IRX` | mc2_d | 3.30 |
| `MODULES/LIBSD.IRX` | Sound_Device_Library | 3.03 |
| `MODULES2/MCMAN.IRX` | mcman | 2.30 |
| `MODULES2/MCSERV.IRX` | mcserv | 2.10 |
| `SG2IOPM1.IRX` | sg2iop_driver | 1.00 |

`IOPRP300.IMG` (ROMDIR) contains: SYSMEM, LOADCORE, SIFCMD, SIFMAN, THREADMAN, IOMAN, MODLOAD,
FILEIO, CDVDMAN, CDVDFSV, LOADFILE, TIMEMANI, ROMDRV, EESYNC, SYSCLIB, STDIO.

The boot ELF also references network modules (`INET.IRX`, `NETCNF.IRX`, `SMAP.IRX`, `PPP*.IRX`,
...) that are **not present on the disc**; the network start-up path is unused in the retail build.

Boot order observed in PCSX2: IOP reboot with `IOPRP300.IMG`, then `SIO2MAN`, `DBCMAN`, `SIO2D`,
`DS1O_D`, later `LIBSD` and the `sg2iop_driver`. The game installs its own TLB refill handler
(`0x8010F300`, i.e. boot ELF code at `0x0010F300`).

## Reference environment

| Item | Value |
|---|---|
| PCSX2 | 2.8.2 (portable, `Emulator/`), BIOS SCPH-70012 v12 USA 2.00 configured |
| Host OS | Windows 11 Home 10.0.26200 |
| Compiler | MSVC 14.44.35207 (Visual Studio 2022 Community), bundled CMake 3.31.6, Ninja 1.12.1 |
| Python | 3.10.6 |
| Java | 17.0.12 (Ghidra not installed; not needed because the modules carry symbols) |
| PS2Recomp | `ran-j/PS2Recomp` `75d729ce40d7eed9649fd4bb05628dee520f3d0c` (2026-09-19) + local branch `sotc-port` |
