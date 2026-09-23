import argparse
import hashlib
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from xff import XffModule

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def regions(layout_path, extracted):
    layout = json.load(open(layout_path))
    out = [
        ("BOOT .text", 0x100000, 0x2EAEC),
        ("BOOT .data..sdata", 0x12EB80, 0x136E80 - 0x12EB80),
        ("BOOT .sbss/.bss", 0x136E80, 0x164FE8 - 0x136E80),
    ]
    for m in layout["modules"]:
        mod = XffModule(m["name"], open(os.path.join(extracted, m["name"] + ".XFF"), "rb").read())
        base = int(m["base"], 16)
        for s in mod.sections:
            if s.is_alloc_progbits and s.name in (".text", ".data", ".rodata", ".vutext"):
                out.append((f"{m['name']} {s.name}", base + s.image_addr, s.size))
            if s.is_nobits and s.size:
                out.append((f"{m['name']} .bss", int(m["bss"], 16), s.size))
    return out


def main():
    ap = argparse.ArgumentParser(description="Compare two 32 MB EE RAM dumps region by region")
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--layout", default=os.path.join(ROOT, "build", "link", "sotc_layout.json"))
    ap.add_argument("--extracted", default=os.path.join(ROOT, "Analysis", "extracted"))
    ap.add_argument("--show", type=int, default=6)
    args = ap.parse_args()

    a = open(args.a, "rb").read()
    b = open(args.b, "rb").read()
    for name, start, size in regions(args.layout, args.extracted):
        ra = a[start:start + size]
        rb = b[start:start + size]
        diff = [o for o in range(0, size - 3, 4) if ra[o:o + 4] != rb[o:o + 4]]
        status = "identical" if not diff else f"{len(diff)} words differ"
        print(f"{name:22s} {start:#010x}+{size:#09x}  {status}")
        for o in diff[:args.show]:
            print(f"      {start + o:#010x}: {struct.unpack_from('<I', ra, o)[0]:08x} vs {struct.unpack_from('<I', rb, o)[0]:08x}")
    print(f"whole RAM sha256: {hashlib.sha256(a).hexdigest()[:16]} vs {hashlib.sha256(b).hexdigest()[:16]}")


if __name__ == "__main__":
    main()
