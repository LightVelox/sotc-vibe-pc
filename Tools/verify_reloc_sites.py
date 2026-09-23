import argparse
import bisect
import csv
import glob
import os
import re
import sys


def main():
    ap = argparse.ArgumentParser(description="Check that every runtime relocation site is decoded from guest RAM in generated code")
    ap.add_argument("relocs")
    ap.add_argument("generated")
    ap.add_argument("functions")
    args = ap.parse_args()

    sites = {int(r["address"], 16): r for r in csv.DictReader(open(args.relocs))}
    funcs = [(int(r["start"], 16), int(r["end"], 16)) for r in csv.DictReader(open(args.functions))]
    covered = set()
    pat = re.compile(r"FAST_READ32\(0x([0-9A-F]+)u\)")
    for path in glob.glob(os.path.join(args.generated, "*.cpp")):
        with open(path, encoding="utf-8", errors="replace") as f:
            for m in pat.finditer(f.read()):
                covered.add(int(m.group(1), 16))

    hle_starts = set()
    stub_header = os.path.join(args.generated, "ps2_recompiled_stubs.h")
    if os.path.exists(stub_header):
        for m in re.finditer(r"void \w+_0x([0-9a-fA-F]+)\(", open(stub_header).read()):
            hle_starts.add(int(m.group(1), 16))
    starts = sorted(funcs)
    missing = []
    outside = []
    in_hle = []
    for a in sorted(sites):
        if a in covered:
            continue
        i = bisect.bisect_right(starts, (a, 0xFFFFFFFF)) - 1
        if i < 0 or not (starts[i][0] <= a < starts[i][1]):
            outside.append(a)
        elif starts[i][0] in hle_starts:
            in_hle.append(a)
        else:
            missing.append(a)
    print(f"sites: {len(sites)} covered: {len(covered & set(sites))} inside-HLE-bound-functions: {len(in_hle)} "
          f"missing-inside-functions: {len(missing)} outside-any-function: {len(outside)}")
    for a in missing[:20]:
        print(f"  MISSING {a:#010x} {sites[a]['kind']} {sites[a]['module']} {sites[a]['symbol']}")
    for a in outside[:10]:
        print(f"  OUTSIDE {a:#010x} {sites[a]['kind']} {sites[a]['module']} {sites[a]['symbol']}")
    sys.exit(1 if missing else 0)


if __name__ == "__main__":
    main()
