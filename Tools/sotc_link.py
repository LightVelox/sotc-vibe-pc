import argparse
import bisect
import csv
import hashlib
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from iso9660 import Iso9660
from xff import (Linker, XffModule, R_MIPS_26, R_MIPS_32, R_MIPS_HI16, R_MIPS_LO16,
                 SHN_ABS, SHN_UNDEF, STT_FUNC, STT_NOTYPE, STT_SECTION)

NOP = 0
JR_RA = 0x03E00008
BOOT_EXTRA_ENTRIES = {
    0x001198DC: "sub_001198DC",
    0x0011F420: "_sceMcCoreRpcEnd",
}


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def hexint(s):
    return int(s, 0) if isinstance(s, str) else s


def sanitize(name):
    return re.sub(r"[^A-Za-z0-9_]", "_", name)


class CodeRegion:
    def __init__(self, owner, name, addr, data):
        self.owner = owner
        self.name = name
        self.addr = addr
        self.data = data

    @property
    def end(self):
        return self.addr + len(self.data)

    def contains(self, a):
        return self.addr <= a < self.end

    def word(self, a):
        return struct.unpack_from("<I", self.data, a - self.addr)[0]


class FunctionInfo:
    def __init__(self, start, name, source, confidence, size=0):
        self.start = start
        self.name = name
        self.source = source
        self.confidence = confidence
        self.size = size
        self.end = 0
        self.module = ""
        self.callers = set()
        self.callees = set()


def is_function_boundary(region, t):
    if not region.contains(t) or t == region.addr:
        return t == region.addr
    q = t
    while q - 4 >= region.addr and region.word(q - 4) == NOP:
        q -= 4
    for back in (8, 4):
        if q - back < region.addr:
            continue
        w = region.word(q - back)
        if (w & 0xFC1FFFFF) == 0x00000008 or (w >> 26) == 2:
            return True
    return False


class Discovery:
    def __init__(self, region):
        self.region = region
        self.funcs = {}
        self.data_pointer_tables = []

    def add(self, start, name, source, confidence, size=0):
        if not self.region.contains(start) or start & 3:
            return
        f = self.funcs.get(start)
        if f is None:
            self.funcs[start] = FunctionInfo(start, name, source, confidence, size)
        elif not f.name.startswith("sub_") or name.startswith("sub_"):
            if source not in f.source.split("+"):
                f.source += "+" + source
        else:
            f.name, f.size = name, size
            f.source = source + "+" + f.source
            f.confidence = confidence

    def named_ranges(self):
        return sorted((f.start, f.start + f.size) for f in self.funcs.values() if f.size)

    def inside_named(self, a):
        rng = self.named_ranges()
        starts = [r[0] for r in rng]
        i = bisect.bisect_right(starts, a) - 1
        return i >= 0 and rng[i][0] < a < rng[i][1]

    def add_gap_starts(self):
        r = self.region
        for s, e in self.named_ranges():
            a = e
            while a < r.end and r.word(a) == NOP:
                a += 4
            if a < r.end and a not in self.funcs:
                self.add(a, f"sub_{a:08X}", "after-named", "medium")

    def resolve_pointer_tables(self):
        starts = sorted(self.funcs)
        for table in self.data_pointer_tables:
            targets = [t for t in table if self.region.contains(t)]
            if not targets:
                continue
            idx = {bisect.bisect_right(starts, t) - 1 for t in targets}
            inside = all(starts and bisect.bisect_right(starts, t) - 1 >= 0 and starts[bisect.bisect_right(starts, t) - 1] < t for t in targets)
            if len(idx) == 1 and inside:
                continue
            for t in targets:
                if not self.inside_named(t):
                    self.add(t, f"sub_{t:08X}", "ptr-table", "medium")

    def add_boundary_pointers(self):
        for table in self.data_pointer_tables:
            for t in table:
                if self.region.contains(t) and t not in self.funcs and not self.inside_named(t) and is_function_boundary(self.region, t):
                    self.add(t, f"sub_{t:08X}", "ptr-boundary", "medium")

    def finalize(self):
        r = self.region
        starts = sorted(self.funcs)
        for i, s in enumerate(starts):
            f = self.funcs[s]
            nxt = starts[i + 1] if i + 1 < len(starts) else r.end
            f.end = nxt
            if f.size and s + f.size < nxt:
                f.end = s + f.size
            f.module = r.owner
        return [self.funcs[s] for s in starts]


def group_tables(pointers):
    tables = []
    cur = []
    last = None
    for loc, tgt in sorted(pointers):
        if last is not None and loc == last + 4:
            cur.append(tgt)
        else:
            if cur:
                tables.append(cur)
            cur = [tgt]
        last = loc
    if cur:
        tables.append(cur)
    return tables


def load_inputs(args, profile):
    files = {}
    names = [profile["boot_elf"]["path"]] + [m["path"] for m in profile["modules"]]
    if args.iso:
        iso = Iso9660(args.iso)
        for n in names:
            e = iso.find(n)
            if not e:
                sys.exit(f"{n} not found in {args.iso}")
            iso.f.seek(e.lba * 2048)
            files[n] = iso.f.read(e.size)
    else:
        for n in names:
            files[n] = open(os.path.join(args.files, n), "rb").read()
    expect = {profile["boot_elf"]["path"]: profile["boot_elf"]["sha256"]}
    for m in profile["modules"]:
        expect[m["path"]] = m["sha256"]
    for n, h in expect.items():
        got = sha256(files[n])
        if got != h:
            sys.exit(f"{n}: sha256 mismatch (got {got}, expected {h}). This tool only supports {profile['serial']} v{profile['version']}.")
    return files


def parse_boot_elf(data):
    e_entry, e_phoff, e_shoff = struct.unpack_from("<III", data, 24)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", data, 46)
    shdrs = [struct.unpack_from("<10I", data, e_shoff + i * e_shentsize) for i in range(e_shnum)]
    stroff = shdrs[e_shstrndx][4]
    secs = []
    for s in shdrs:
        end = data.index(b"\0", stroff + s[0])
        name = data[stroff + s[0]:end].decode()
        secs.append({"name": name, "type": s[1], "flags": s[2], "addr": s[3], "offset": s[4], "size": s[5]})
    return e_entry, secs


def discover_boot(elf, secs, kernel, entry):
    text = next(s for s in secs if s["name"] == ".text")
    region = CodeRegion("BOOT", ".text", text["addr"], elf[text["offset"]:text["offset"] + text["size"]])
    d = Discovery(region)
    d.add(entry, f"sub_{entry:08X}", "elf-entry", "high")
    for address, name in BOOT_EXTRA_ENTRIES.items():
        d.add(address, name, "manual", "high")
    for s in kernel.symbols:
        if s.shndx == SHN_ABS and s.name and region.contains(s.value):
            if s.type == STT_FUNC:
                d.add(s.value, s.name, "xff-abs-symbol", "high", s.size)
            elif s.type == STT_NOTYPE:
                d.add(s.value, s.name, "xff-abs-label", "high", 0)
    for a in range(region.addr, region.end, 4):
        w = region.word(a)
        op = w >> 26
        if op == 3:
            t = ((w & 0x03FFFFFF) << 2) | ((a + 4) & 0xF0000000)
            if region.contains(t) and not d.inside_named(t):
                d.add(t, f"sub_{t:08X}", "jal", "high")
    for a in range(region.addr, region.end, 4):
        w = region.word(a)
        if w >> 26 == 2:
            t = ((w & 0x03FFFFFF) << 2) | ((a + 4) & 0xF0000000)
            if region.contains(t) and t not in d.funcs and not d.inside_named(t):
                starts = sorted(d.funcs)
                i = bisect.bisect_right(starts, a) - 1
                j = bisect.bisect_right(starts, t) - 1
                if i != j:
                    d.add(t, f"sub_{t:08X}", "j-tail", "medium")
    ptrs = []
    for s in secs:
        if s["type"] == 1 and s["name"] in (".data", ".rodata", ".sdata"):
            blob = elf[s["offset"]:s["offset"] + s["size"]]
            for o in range(0, len(blob) - 3, 4):
                v = struct.unpack_from("<I", blob, o)[0]
                if region.contains(v) and v % 4 == 0:
                    ptrs.append((s["addr"] + o, v))
    for a in range(region.addr, region.end - 4, 4):
        w = region.word(a)
        if w >> 26 == 0x0F:
            rt = (w >> 16) & 31
            hi = (w & 0xFFFF) << 16
            for b in range(a + 4, min(a + 32, region.end), 4):
                w2 = region.word(b)
                if w2 >> 26 == 0x09 and (w2 >> 21) & 31 == rt:
                    lo = w2 & 0xFFFF
                    v = (hi + (lo - 0x10000 if lo & 0x8000 else lo)) & 0xFFFFFFFF
                    if region.contains(v) and not d.inside_named(v):
                        d.add(v, f"sub_{v:08X}", "la-pair", "medium")
                    break
    d.data_pointer_tables = group_tables(ptrs)
    d.resolve_pointer_tables()
    d.add_boundary_pointers()
    d.add_gap_starts()
    return region, d.finalize()


def discover_module(lm, profile_mod):
    m = lm.module
    out = []
    text_like = [s for s in m.sections if s.is_alloc_progbits and s.name == ".text"]
    for tsec in text_like:
        base = lm.section_addr[tsec.index]
        region = CodeRegion(m.name, tsec.name, base, bytes(lm.image[tsec.image_addr:tsec.image_addr + tsec.size]))
        d = Discovery(region)
        for s in m.symbols:
            if s.shndx == tsec.index and s.name and s.type == STT_FUNC:
                d.add(base + s.value, s.name, "xff-symbol", "high", s.size)
        for s in m.symbols:
            if s.shndx == tsec.index and s.name and s.type != STT_FUNC and s.type != STT_SECTION:
                d.add(base + s.value, s.name, "xff-label", "high", 0)
        ptrs_rodata = []
        for table in m.reloc_tables:
            src = m.sections[table.target_section]
            src_base = lm.section_addr.get(src.index, 0)
            pending = []
            for off, si, rt, orig in table.entries:
                sym = m.symbols[si]
                if sym.shndx != tsec.index:
                    if rt == R_MIPS_HI16:
                        pending = []
                    continue
                sv = base + sym.value
                if rt == R_MIPS_26:
                    t = ((orig & 0x03FFFFFF) << 2) + sv
                    if orig >> 26 == 3:
                        d.add(t, f"sub_{t:08X}", "reloc-jal", "high")
                    else:
                        d.add(t, f"sub_{t:08X}", "reloc-j", "medium") if not d.inside_named(t) else None
                elif rt == R_MIPS_32:
                    t = (orig + sv) & 0xFFFFFFFF
                    if src.name == ".rodata":
                        ptrs_rodata.append((src_base + off, t))
                    elif not d.inside_named(t):
                        d.add(t, f"sub_{t:08X}", "reloc-data-ptr", "high")
                elif rt == R_MIPS_HI16:
                    pending.append(orig)
                elif rt == R_MIPS_LO16:
                    lo = orig & 0xFFFF
                    lo = lo - 0x10000 if lo & 0x8000 else lo
                    for h in pending:
                        t = (((h & 0xFFFF) << 16) + lo + sv) & 0xFFFFFFFF
                        if not d.inside_named(t):
                            d.add(t, f"sub_{t:08X}", "reloc-la", "high")
                    pending = []
        d.data_pointer_tables = group_tables(ptrs_rodata)
        d.resolve_pointer_tables()
        d.add_gap_starts()
        out.append((region, d.finalize()))
    return out


def runtime_reloc_sites(lm):
    m = lm.module
    sites = []
    for table in m.reloc_tables:
        sec = m.sections[table.target_section]
        if sec.name != ".text" or not sec.image_addr:
            continue
        base = lm.section_addr[sec.index]
        for off, si, rt, orig in table.entries:
            sym = m.symbols[si]
            if sym.shndx != SHN_UNDEF:
                continue
            kind = {R_MIPS_26: "j26", R_MIPS_HI16: "hi16", R_MIPS_LO16: "lo16", R_MIPS_32: "w32"}.get(rt)
            sites.append((base + off, kind, m.name, sym.name))
    return sites


class ElfWriter:
    def __init__(self, entry, flags):
        self.entry = entry
        self.flags = flags
        self.sections = []
        self.symbols = []

    def add_section(self, name, addr, data, executable, writable):
        self.sections.append((name, addr, bytes(data), executable, writable))

    def add_symbol(self, name, addr, size, is_func, section_name):
        self.symbols.append((name, addr, size, is_func, section_name))

    def write(self, path):
        shstr = bytearray(b"\0")
        strtab = bytearray(b"\0")

        def add_str(tab, s):
            off = len(tab)
            tab += s.encode() + b"\0"
            return off

        body = bytearray()
        hdr_size = 52
        shdrs = [(0, 0, 0, 0, 0, 0, 0, 0, 0, 0)]
        sec_index = {}
        phdrs = []
        for name, addr, data, ex, wr in self.sections:
            while (hdr_size + len(body)) % 16:
                body += b"\0"
            off = hdr_size + len(body)
            body += data
            flags = 0x2 | (0x4 if ex else 0) | (0x1 if wr else 0)
            sec_index[name] = len(shdrs)
            shdrs.append((add_str(shstr, name), 1, flags, addr, off, len(data), 0, 0, 16, 0))
            phdrs.append((1, off, addr, addr, len(data), len(data), 5 if ex else 6, 16))
        symtab = bytearray(16)
        for name, addr, size, is_func, secname in sorted(self.symbols, key=lambda s: s[1]):
            info = (1 << 4) | (STT_FUNC if is_func else 1)
            symtab += struct.pack("<IIIBBH", add_str(strtab, name), addr, size, info, 0, sec_index[secname])
        while (hdr_size + len(body)) % 4:
            body += b"\0"
        symtab_off = hdr_size + len(body)
        body += symtab
        strtab_off = hdr_size + len(body)
        body += strtab
        strtab_idx = len(shdrs) + 1
        shdrs.append((add_str(shstr, ".symtab"), 2, 0, 0, symtab_off, len(symtab), strtab_idx, 1, 4, 16))
        shdrs.append((add_str(shstr, ".strtab"), 3, 0, 0, strtab_off, len(strtab), 0, 0, 1, 0))
        shstr_name = add_str(shstr, ".shstrtab")
        shstr_off = hdr_size + len(body)
        body += shstr
        shdrs.append((shstr_name, 3, 0, 0, shstr_off, len(shstr), 0, 0, 1, 0))
        while (hdr_size + len(body)) % 4:
            body += b"\0"
        ph_off = hdr_size + len(body)
        for p in phdrs:
            body += struct.pack("<8I", *p)
        sh_off = hdr_size + len(body)
        for s in shdrs:
            body += struct.pack("<10I", *s)
        ident = b"\x7fELF\x01\x01\x01" + b"\0" * 9
        hdr = ident + struct.pack("<HHIIIIIHHHHHH", 2, 8, 1, self.entry, ph_off, sh_off, self.flags,
                                  52, 32, len(phdrs), 40, len(shdrs), len(shdrs) - 1)
        with open(path, "wb") as f:
            f.write(hdr + body)


def verify_against_ram(linker, ram_path, report):
    ram = open(ram_path, "rb").read()
    ok = True
    for lm in linker.modules:
        for s in lm.module.sections:
            if s.is_alloc_progbits and s.name in (".text", ".rodata", ".vutext"):
                a = lm.base + s.image_addr
                mine = lm.image[s.image_addr:s.image_addr + s.size]
                theirs = ram[a:a + s.size]
                diff = [i for i in range(0, s.size - 3, 4) if mine[i:i + 4] != theirs[i:i + 4]]
                report.append(f"verify {lm.module.name}{s.name} @{a:#010x} size={s.size:#x}: {len(diff)} differing words")
                for i in diff[:8]:
                    report.append(f"    {a + i:#010x}: linked={mine[i:i + 4].hex()} oracle={theirs[i:i + 4].hex()}")
                ok &= not diff
    return ok


def masked_text(lm, site_set):
    t = lm.module.section_by_name(".text")
    base = lm.section_addr[t.index]
    data = bytearray(lm.image[t.image_addr:t.image_addr + t.size])
    for a in site_set:
        if base <= a < base + t.size:
            data[a - base:a - base + 4] = bytes(4)
    return base, t.size, sha256(bytes(data))


def emit_layout_header(path, profile, linker, sites):
    site_set = sorted({s[0] for s in sites})
    lines = [
        "#pragma once",
        "#include <cstdint>",
        "",
        "namespace sotc::generated",
        "{",
        "    struct ModuleLayout",
        "    {",
        "        const char *name;",
        "        uint32_t base;",
        "        uint32_t bss;",
        "        uint32_t entry;",
        "        uint32_t text;",
        "        uint32_t textSize;",
        "        const char *maskedTextSha256;",
        "    };",
        "",
        f'    inline constexpr const char *kSerial = "{profile["serial"]}";',
        f'    inline constexpr const char *kVersion = "{profile["version"]}";',
        f'    inline constexpr uint64_t kDiscSize = {profile["disc"]["size"]}ull;',
        f'    inline constexpr const char *kBootElfPath = "{profile["boot_elf"]["path"]}";',
        f'    inline constexpr const char *kBootElfSha256 = "{profile["boot_elf"]["sha256"]}";',
        f'    inline constexpr uint32_t kUndefinedImportStub = {hexint(profile["undefined_import_stub"]):#010x}u;',
        "",
        "    inline constexpr ModuleLayout kModules[] = {",
    ]
    for lm, p in zip(linker.modules, profile["modules"]):
        base, size, h = masked_text(lm, site_set)
        lines.append(f'        {{"{lm.module.name}", {lm.base:#010x}u, {lm.bss_addr:#010x}u, {lm.entry:#010x}u, {base:#010x}u, {size:#x}u, "{h}"}},')
    lines += ["    };", "", "    inline constexpr struct { const char *path; const char *sha256; } kModuleFiles[] = {"]
    for p in profile["modules"]:
        lines.append(f'        {{"{p["path"]}", "{p["sha256"]}"}},')
    lines += ["    };", "", "    inline constexpr uint32_t kRuntimeRelocationSites[] = {"]
    for i in range(0, len(site_set), 8):
        lines.append("        " + " ".join(f"{a:#010x}u," for a in site_set[i:i + 8]))
    lines += ["    };", "}", ""]
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w", newline="") as f:
        f.write("\n".join(lines))


def main():
    ap = argparse.ArgumentParser(description="Link Shadow of the Colossus boot ELF + XFF modules into a recompilation image")
    ap.add_argument("--profile", required=True)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--iso")
    src.add_argument("--files")
    ap.add_argument("--out", required=True)
    ap.add_argument("--verify-ram")
    ap.add_argument("--emit-header")
    args = ap.parse_args()

    profile = json.load(open(args.profile))
    os.makedirs(args.out, exist_ok=True)
    files = load_inputs(args, profile)
    report = [f"profile {profile['serial']} v{profile['version']}"]

    boot = files[profile["boot_elf"]["path"]]
    entry, secs = parse_boot_elf(boot)
    elf_flags = struct.unpack_from("<I", boot, 36)[0]
    modules = [(p, XffModule(p["name"], files[p["path"]])) for p in profile["modules"]]
    kernel = modules[0][1]

    linker = Linker()
    stub = hexint(profile["undefined_import_stub"])
    for p, m in modules:
        for s in m.exported_symbols():
            if s.shndx == SHN_ABS:
                linker.global_symbols.setdefault(s.name, s.value)
    pre = Linker()
    for p, m in modules:
        pre.link(m, hexint(p["base"]), hexint(p["bss"]))
    all_exports = dict(pre.global_symbols)

    class StubLinker(Linker):
        def resolve(self, lm, sym):
            if sym.shndx == SHN_UNDEF and sym.name not in self.global_symbols:
                return stub
            return super().resolve(lm, sym)

    linker2 = StubLinker()
    linker2.global_symbols = {}
    first_pass_available = {}
    for p, m in modules:
        linker2.link(m, hexint(p["base"]), hexint(p["bss"]))
    linker = linker2

    if args.verify_ram:
        ok = verify_against_ram(linker, args.verify_ram, report)
        report.append("verification: " + ("PASS" if ok else "DIFFERENCES (see above)"))

    w = ElfWriter(entry, elf_flags)
    for s in secs:
        if s["type"] == 1 and s["flags"] & 2 and s["size"]:
            w.add_section("boot" + s["name"], s["addr"], boot[s["offset"]:s["offset"] + s["size"]],
                          bool(s["flags"] & 4), bool(s["flags"] & 1))
    for lm in linker.modules:
        for s in lm.module.sections:
            if s.is_alloc_progbits and s.name in (".text", ".data", ".rodata", ".vutext"):
                w.add_section(lm.module.name.lower() + s.name, lm.base + s.image_addr,
                              lm.image[s.image_addr:s.image_addr + s.size],
                              s.name in (".text", ".vutext"), s.name == ".data")

    region, boot_funcs = discover_boot(boot, secs, kernel, entry)
    all_funcs = [(region, "boot.text", boot_funcs)]
    for lm, (p, m) in zip(linker.modules, modules):
        for r, fl in discover_module(lm, p):
            all_funcs.append((r, m.name.lower() + ".text", fl))

    used = {}
    rows = []
    for region, secname, fl in all_funcs:
        for f in fl:
            base = sanitize(f.name)
            if base in used:
                used[base] += 1
                base = f"{base}__{f.module}_{f.start:08X}"
            else:
                used[base] = 1
            f.name = base
            w.add_symbol(f.name, f.start, f.end - f.start, True, secname)
            rows.append(f)
    w.write(os.path.join(args.out, "sotc_linked.elf"))

    with open(os.path.join(args.out, "sotc_functions.csv"), "w", newline="") as fcsv:
        cw = csv.writer(fcsv)
        cw.writerow(["name", "start", "end", "size"])
        for f in rows:
            cw.writerow([f.name, f"0x{f.start:08X}", f"0x{f.end:08X}", f.end - f.start])

    sites = []
    for lm in linker.modules:
        sites += runtime_reloc_sites(lm)
    with open(os.path.join(args.out, "sotc_runtime_relocs.csv"), "w", newline="") as rcsv:
        cw = csv.writer(rcsv)
        cw.writerow(["address", "kind", "module", "symbol"])
        for a, k, mod, sym in sorted(sites):
            cw.writerow([f"0x{a:08X}", k, mod, sym])

    with open(os.path.join(args.out, "sotc_address_map.csv"), "w", newline="") as acsv:
        cw = csv.writer(acsv)
        cw.writerow(["address", "end", "module", "generated_name", "provisional_name", "name_source", "confidence", "replacement_status", "notes"])
        for f in rows:
            prov = "" if f.name.startswith("sub_") else f.name
            cw.writerow([f"0x{f.start:08X}", f"0x{f.end:08X}", f.module, f"sub_{f.start:08X}", prov, f.source, f.confidence, "recomp", ""])

    layout = {
        "boot_entry": f"0x{entry:08X}",
        "modules": [{"name": lm.module.name, "base": f"0x{lm.base:08X}", "bss": f"0x{lm.bss_addr:08X}", "entry": f"0x{lm.entry:08X}",
                     "text": f"0x{lm.section_addr[lm.module.section_by_name('.text').index]:08X}",
                     "text_size": lm.module.section_by_name(".text").size,
                     "text_sha256": sha256(lm.image[lm.module.section_by_name('.text').image_addr:lm.module.section_by_name('.text').image_addr + lm.module.section_by_name('.text').size])}
                    for lm in linker.modules],
        "function_count": len(rows),
        "runtime_reloc_sites": len(sites),
    }
    json.dump(layout, open(os.path.join(args.out, "sotc_layout.json"), "w"), indent=2)
    if args.emit_header:
        emit_layout_header(args.emit_header, profile, linker, sites)

    by_src = {}
    for f in rows:
        by_src[f.source] = by_src.get(f.source, 0) + 1
    report.append(f"functions: {len(rows)} ({sum(1 for f in rows if not f.name.startswith('sub_'))} named)")
    for k, v in sorted(by_src.items(), key=lambda x: -x[1]):
        report.append(f"    {k}: {v}")
    report.append(f"runtime relocation sites in code: {len(sites)}")
    unresolved = sorted({s[3] for s in sites if s[3] not in all_exports})
    report.append(f"imports bound to undefined stub at link time: {len(unresolved)}")
    open(os.path.join(args.out, "link_report.txt"), "w").write("\n".join(report) + "\n")
    print("\n".join(report))


if __name__ == "__main__":
    main()
