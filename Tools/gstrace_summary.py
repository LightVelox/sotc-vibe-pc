import re
import sys


def parse(line):
    head, *verts = line.rstrip("\n").split(" | ")
    fields = dict(kv.split("=", 1) for kv in head.split()[1:])
    vs = []
    for v in verts:
        m = re.match(r"([-\d.]+),([-\d.]+) z=(\S+) rgba=(\w+) uv=([-\d.]+),([-\d.]+) st=(\S+),(\S+) q=(\S+)", v)
        vs.append(m.groups())
    return fields, vs


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: gstrace_summary.py <trace.txt> [vsync tick]")
    path = sys.argv[1]
    tick = sys.argv[2] if len(sys.argv) > 2 else None
    active = tick is None
    prev = None
    count = 0
    last = ""
    for line in open(path):
        if line.startswith("V "):
            active = tick is None or line.startswith(f"V tick={tick} ")
            if active:
                print(line.strip())
            continue
        if not active:
            continue
        if line.startswith("T "):
            if count:
                print(f"{count:4d}x {last}")
                count = 0
                prev = None
            print("     " + line.strip())
            continue
        if not line.startswith("D "):
            continue
        f, vs = parse(line)
        ofx, ofy = (int(x) / 16 for x in f["ofs"].split(","))
        xs = [float(v[0]) - ofx for v in vs]
        ys = [float(v[1]) - ofy for v in vs]
        us = [float(v[4]) for v in vs]
        vv = [float(v[5]) for v in vs]
        tex = f"tex={f['tex0']}" if f["tme"] == "1" else "notex"
        key = (f"p{f['prim']} tme{f['tme']} abe{f['abe']} fst{f['fst']} ctx{f['ctxt']} fb={f['frame']} z={f['zbuf']} {tex} "
               f"alpha={f['alpha']} test={f['test']} fba={f['fba']} clamp={f['clamp']} tex1={f['tex1']} sc={f['scissor']}")
        desc = (f"{key} rect=({min(xs):.1f},{min(ys):.1f})-({max(xs):.1f},{max(ys):.1f}) "
                f"uv=({min(us):.1f},{min(vv):.1f})-({max(us):.1f},{max(vv):.1f}) rgba={vs[0][3]}")
        if key == prev:
            count += 1
            continue
        if count:
            print(f"{count:4d}x {last}")
        prev = key
        count = 1
        last = desc
    if count:
        print(f"{count:4d}x {last}")


if __name__ == "__main__":
    main()
