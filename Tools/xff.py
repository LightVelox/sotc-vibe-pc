import struct

R_MIPS_NONE = 0
R_MIPS_32 = 2
R_MIPS_26 = 4
R_MIPS_HI16 = 5
R_MIPS_LO16 = 6

SHT_NOBITS = 8
SHN_UNDEF = 0
SHN_ABS = 0xFFF1

STT_NOTYPE = 0
STT_OBJECT = 1
STT_FUNC = 2
STT_SECTION = 3


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


class XffSection:
    def __init__(self, index, name, runtime_addr, image_addr, size, align, type_, flags, file_offset):
        self.index = index
        self.name = name
        self.runtime_addr = runtime_addr
        self.image_addr = image_addr
        self.size = size
        self.align = align
        self.type = type_
        self.flags = flags
        self.file_offset = file_offset

    @property
    def is_nobits(self):
        return self.type == SHT_NOBITS

    @property
    def is_alloc_progbits(self):
        return self.type == 1 and self.image_addr != 0 and self.size > 0


class XffSymbol:
    def __init__(self, index, name, value, size, bind, type_, shndx):
        self.index = index
        self.name = name
        self.value = value
        self.size = size
        self.bind = bind
        self.type = type_
        self.shndx = shndx


class XffRelocTable:
    def __init__(self, target_section, entries):
        self.target_section = target_section
        self.entries = entries


class XffModule:

    def __init__(self, name, data):
        if data[:4] != b"xff2":
            raise ValueError(f"{name}: not an xff2 module")
        self.name = name
        self.data = bytearray(data)
        d = self.data
        self.first_global = u32(d, 0x0C)
        self.file_size = u32(d, 0x14)
        self.num_imports = u32(d, 0x1C)
        self.num_symbols = u32(d, 0x24)
        self.num_reloc_tables = u32(d, 0x38)
        self.num_sections = u32(d, 0x40)
        self.entry_offset = u32(d, 0x4C)
        self.symtab_off = u32(d, 0x54)
        self.strtab_off = u32(d, 0x58)
        self.shdr_off = u32(d, 0x5C)
        self.symvalue_off = u32(d, 0x60)
        self.reldesc_off = u32(d, 0x64)
        self.secname_idx_off = u32(d, 0x68)
        self.secname_str_off = u32(d, 0x6C)
        self._parse_sections()
        self._parse_symbols()
        self._parse_relocs()

    def _cstr(self, off):
        end = self.data.index(b"\0", off)
        return self.data[off:end].decode("latin1")

    def _parse_sections(self):
        self.sections = []
        for i in range(self.num_sections):
            row = struct.unpack_from("<8I", self.data, self.shdr_off + 32 * i)
            name = self._cstr(self.secname_str_off + u32(self.data, self.secname_idx_off + 4 * i))
            image_addr = row[1] - 0x40010000 if row[1] >= 0x40010000 else 0
            self.sections.append(XffSection(i, name, row[0], image_addr, row[2], row[3], row[4], row[5], row[7]))

    def _parse_symbols(self):
        self.symbols = []
        for i in range(self.num_symbols):
            n, v, sz, info, _other, shndx = struct.unpack_from("<IIIBBH", self.data, self.symtab_off + 16 * i)
            name = self._cstr(self.strtab_off + n) if n else ""
            self.symbols.append(XffSymbol(i, name, v, sz, info >> 4, info & 15, shndx))

    def _parse_relocs(self):
        self.reloc_tables = []
        for r in range(self.num_reloc_tables):
            _typ, count, target, _p1, _p2, off1, off2 = struct.unpack_from("<7I", self.data, self.reldesc_off + 28 * r)
            entries = []
            for i in range(count):
                offset, info = struct.unpack_from("<II", self.data, off1 + 8 * i)
                orig, _aux = struct.unpack_from("<II", self.data, off2 + 8 * i)
                entries.append((offset, info >> 8, info & 0xFF, orig))
            self.reloc_tables.append(XffRelocTable(target, entries))

    def section_by_name(self, name):
        for s in self.sections:
            if s.name == name:
                return s
        return None

    def exported_symbols(self):
        return [s for s in self.symbols if s.name and s.shndx != SHN_UNDEF and s.bind != 0]


class LinkedModule:
    def __init__(self, module, base, bss_addr):
        self.module = module
        self.base = base
        self.bss_addr = bss_addr
        self.section_addr = {}
        for s in module.sections:
            if s.is_nobits and s.size:
                self.section_addr[s.index] = bss_addr
            elif s.image_addr:
                self.section_addr[s.index] = base + s.image_addr
        self.image = bytearray(module.data)
        self.symbol_addr = {}
        self.entry = None

    def image_addr_of(self, section_index, offset):
        return self.section_addr[section_index] + offset


class Linker:

    def __init__(self):
        self.global_symbols = {}
        self.modules = []
        self.warnings = []

    def add_absolute_symbols(self, symbols):
        for name, addr in symbols.items():
            self.global_symbols.setdefault(name, addr)

    def resolve(self, lm, sym):
        m = lm.module
        if sym.shndx == SHN_ABS:
            return sym.value
        if sym.shndx == SHN_UNDEF:
            if sym.name not in self.global_symbols:
                self.warnings.append(f"{m.name}: unresolved import {sym.name}")
                return 0
            return self.global_symbols[sym.name]
        if sym.shndx not in lm.section_addr:
            return sym.value
        return lm.section_addr[sym.shndx] + sym.value

    def link(self, module, base, bss_addr):
        lm = LinkedModule(module, base, bss_addr)
        for sym in module.symbols:
            if sym.shndx != SHN_UNDEF:
                lm.symbol_addr[sym.index] = self.resolve(lm, sym)
        img = lm.image
        for table in module.reloc_tables:
            tsec = module.sections[table.target_section]
            if not tsec.image_addr:
                continue
            pending_hi = []
            for offset, symidx, rtype, orig in table.entries:
                pos = tsec.image_addr + offset
                sym = module.symbols[symidx]
                s = lm.symbol_addr.get(symidx)
                if s is None:
                    s = self.resolve(lm, sym)
                word = orig
                if rtype == R_MIPS_32:
                    word = (orig + s) & 0xFFFFFFFF
                elif rtype == R_MIPS_26:
                    target = ((orig & 0x03FFFFFF) << 2) + s
                    word = (orig & 0xFC000000) | ((target >> 2) & 0x03FFFFFF)
                elif rtype == R_MIPS_HI16:
                    pending_hi.append((pos, symidx, orig))
                    continue
                elif rtype == R_MIPS_LO16:
                    lo = orig & 0xFFFF
                    lo_s = lo - 0x10000 if lo & 0x8000 else lo
                    for hpos, hsym, horig in pending_hi:
                        if hsym != symidx:
                            self.warnings.append(f"{module.name}: hi16 at {hpos:#x} paired with lo16 of other symbol")
                        ahl = ((horig & 0xFFFF) << 16) + lo_s
                        val = (ahl + s) & 0xFFFFFFFF
                        hi = ((val + 0x8000) >> 16) & 0xFFFF
                        struct.pack_into("<I", img, hpos, (horig & 0xFFFF0000) | hi)
                    pending_hi = []
                    word = (orig & 0xFFFF0000) | ((lo_s + s) & 0xFFFF)
                elif rtype == R_MIPS_NONE:
                    continue
                else:
                    raise ValueError(f"{module.name}: unsupported relocation type {rtype} at {pos:#x}")
                struct.pack_into("<I", img, pos, word)
            if pending_hi:
                self.warnings.append(f"{module.name}: {len(pending_hi)} hi16 relocations without lo16")
        text = module.section_by_name(".text")
        lm.entry = lm.section_addr[text.index] + module.entry_offset
        for sym in module.exported_symbols():
            self.global_symbols[sym.name] = lm.symbol_addr[sym.index]
        self.modules.append(lm)
        return lm
