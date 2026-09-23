# The `xff2` module format (Shadow of the Colossus, SCES-53326)

Reverse-engineered from the files on the disc and verified against PCSX2 guest memory. Implemented by
`Tools/xff.py` (parser + relocating linker) and `Tools/sotc_link.py` (whole-program link for
recompilation).

`xff2` is a container around the contents of a relocatable MIPS ELF (`ld -r` output): it keeps the
ELF section contents, the ELF symbol table, and REL relocations, but replaces the ELF headers with
its own header that the loader in `SCES_533.26` can relocate in place. All values are little endian.

## Header

Pointer fields are stored biased by `0x40010000` in the file (`0x40010000 + file offset`); the
loader rebases them to `image_base + file offset` when it loads the image.

| Offset | Meaning |
|---|---|
| `0x00` | magic `"xff2"` |
| `0x0C` | index of the first non-local symbol |
| `0x10` | entry address (0 in file, written by the loader: `.text` address + entry offset) |
| `0x14` | total file size |
| `0x18` | end of loaded image (written by the loader) |
| `0x1C` | number of undefined (imported) symbols |
| `0x20` | pointer: section name block |
| `0x24` | symbol count |
| `0x28` | pointer: `.symtab` |
| `0x2C` | pointer: `.strtab` |
| `0x30` | pointer: section header table |
| `0x34` | pointer: per-symbol resolved-value array |
| `0x38` | relocation table count |
| `0x3C` | pointer: relocation table descriptors |
| `0x40` | section count |
| `0x44` | pointer: section name index array |
| `0x48` | pointer: section name strings |
| `0x4C` | entry point offset inside `.text` |
| `0x50..0x6C` | the same eight tables as plain file offsets |

## Section headers (32 bytes each)

| Word | Meaning |
|---|---|
| 0 | runtime address (0 in file; written by the loader; `.bss` gets a separately allocated address) |
| 1 | image address (`0x40010000 + file offset` in file) |
| 2 | size |
| 3 | alignment |
| 4 | ELF section type (`1` PROGBITS, `8` NOBITS, `9` REL, `4` RELA, `2` SYMTAB, `3` STRTAB, `0x70000006` REGINFO, `0x7FFFF420/0x7FFFF421` DVP overlay table/overlay) |
| 5 | flags |
| 6 | loader state (becomes 2 for allocated `.bss`) |
| 7 | file offset |

## Symbols

Standard `Elf32_Sym` (16 bytes). `st_value` is section-relative for defined symbols. `SHN_ABS`
symbols in `KERNEL.XFF` name functions and objects of the boot ELF so that later modules can import
them (e.g. `strcpy`, `sceSifCallRpc`). Undefined symbols (`st_shndx = 0`) are imports resolved by
name against symbols exported by previously loaded modules.

## Relocation tables

Descriptor (28 bytes): `type (9 = REL)`, `count`, `target section index`, `ptr entries`,
`ptr originals`, `file offset entries`, `file offset originals`.

* `entries[i]` is a standard `Elf32_Rel` (`r_offset` relative to the target section, `r_info`).
* `originals[i]` holds the **unrelocated** word at that location (plus an auxiliary word). Keeping the
  original makes it possible to re-apply a relocation later with a new symbol value.

Relocation types used: `R_MIPS_32` (2), `R_MIPS_26` (4), `R_MIPS_HI16` (5), `R_MIPS_LO16` (6).
HI16 entries are paired with the following LO16 of the same symbol (several HI16 may share one LO16),
with the usual carry adjustment. Separate tables exist for references to imported symbols.

## Loader behaviour (observed)

* The whole file is read to a heap block (the image base); sections are relocated in place.
* `.bss` is allocated separately. After loading, the relocation tables are no longer needed and their
  memory is reused (e.g. GAMECORE's `.bss` lives in MANAGER's former relocation area).
* `.symtab`/`.strtab` stay resident for linking later modules.
* Imports that cannot be resolved are bound to a trap routine in KERNEL at `0x001B28F8`
  ("undefined function call from %p").
* When later modules (data modules from `NICO.DAT`) define such symbols, the loader patches the code
  that references them, using the saved original words. Consequently instructions at import
  relocation sites in MANAGER/GAMECORE change during the game. The recompiler reads the immediates
  of those 26,981 sites from guest memory at run time (`general.runtime_relocations`).

## Verification

`python Tools/sotc_link.py --profile Port/profile/SCES-53326_v1.00.json --iso <image> --out build/link --verify-ram <PCSX2 RAM dump>`
compares our relocated `.text/.rodata/.vutext` against a 32 MB PCSX2 RAM dump obtained with
`Tools/pine.py dump 0 0x2000000 <file>`. At the title screen: KERNEL identical; MANAGER 133 and
GAMECORE 574 differing words, all at import-relocation sites that were bound by later data modules.
