import argparse
import csv
import os
import struct
import sys

import rabbitizer

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    ap = argparse.ArgumentParser(description="Disassemble a function (or range) of the linked image with symbol names")
    ap.add_argument("start", type=lambda x: int(x, 16))
    ap.add_argument("end", type=lambda x: int(x, 16), nargs="?")
    args = ap.parse_args()
    d = open(os.path.join(ROOT, "build", "link", "sotc_linked.elf"), "rb").read()
    shoff = struct.unpack_from("<I", d, 32)[0]
    n = struct.unpack_from("<H", d, 48)[0]
    secs = [struct.unpack_from("<10I", d, shoff + 40 * i) for i in range(n)]

    def word(a):
        for s in secs:
            if s[1] == 1 and s[3] <= a < s[3] + s[5]:
                return struct.unpack_from("<I", d, s[4] + a - s[3])[0]
        return None

    rows = list(csv.DictReader(open(os.path.join(ROOT, "build", "link", "sotc_address_map.csv"))))
    names = {int(r["address"], 16): (r["provisional_name"] or r["generated_name"]) for r in rows}
    ends = {int(r["address"], 16): int(r["end"], 16) for r in rows}
    end = args.end or ends.get(args.start, args.start + 0x100)
    for a in range(args.start, end, 4):
        w = word(a)
        if w is None:
            break
        text = rabbitizer.Instruction(w, vram=a, category=rabbitizer.InstrCategory.R5900).disassemble()
        if w >> 26 in (2, 3):
            text += "   ; " + names.get(((w & 0x03FFFFFF) << 2) | ((a + 4) & 0xF0000000), "?")
        label = f"{names[a]}:" if a in names else ""
        print(f"{a:08x}: {text:50s} {label}")


if __name__ == "__main__":
    main()
