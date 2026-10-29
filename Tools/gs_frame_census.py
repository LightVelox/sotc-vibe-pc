#!/usr/bin/env python3
"""Summarise what the game draws, from `gs_replay <recording.gsr> --text <dump.txt>` dumps.

  python3 Tools/gs_frame_census.py passes <dump.txt> [frames]   per-frame render passes
  python3 Tools/gs_frame_census.py features <dump.txt>...       GS feature use across all draws

A frame ends with its copy into the display buffer (the game runs at half the field rate, so fields do not
delimit frames). A pass is a run of consecutive draws with the same FRAME/ZBUF target. Texture sources are
found by tracking, per 8 KiB VRAM page, the last writer in stream order: an EE->GS upload, or a render target
whose drawn rectangle (vertex bounds clipped to the scissor) covered the page. Pages written before the
recording started are reported as resident.
"""
import collections
import re
import sys

BPP = {0x00: 4, 0x01: 4, 0x02: 2, 0x0A: 2, 0x13: 1, 0x14: 0.5, 0x1B: 1, 0x24: 0.5, 0x2C: 0.5,
       0x30: 4, 0x31: 4, 0x32: 2, 0x3A: 2}
PRIM = {0: "point", 1: "line", 2: "lstrip", 3: "tri", 4: "tstrip", 5: "tfan", 6: "sprite"}
BLEND_COLOR = ["Cs", "Cd", "0", "?"]
BLEND_ALPHA = ["As", "Ad", "FIX", "?"]

DRAW = re.compile(
    r"prim=(\d+) iip=\d tme=(\d) fge=(\d) abe=(\d) aa1=(\d) fst=\d ctxt=\d fix=\d"
    r" frame=([0-9a-f]+)/(\d+)/([0-9a-f]+)/([0-9a-f]+) zbuf=([0-9a-f]+)/([0-9a-f]+)/(\d)"
    r" tex0=([0-9a-f]+)/(\d+)/([0-9a-f]+)/(\d+)x(\d+)/tcc(\d)/tfx(\d)/cbp[0-9a-f]+/cpsm([0-9a-f]+)/csm\d/csa\d+/cld\d+"
    r" tex1=([0-9a-f]+) clamp=([0-9a-f]+) alpha=([0-9a-f]+) test=([0-9a-f]+) fba=([0-9a-f]+)"
    r" scissor=(\d+)-(\d+),(\d+)-(\d+) ofs=(\d+),(\d+) texa=\d+/(-?\d+)/\d+ pabe=(\d) dthe=([0-9a-f]+) colclamp=([0-9a-f]+)")
VERTEX = re.compile(r"\| (-?[0-9.]+),(-?[0-9.]+) ")
TRANSFER = re.compile(r"T dir=(\d+) src=[0-9a-f]+/\d+/[0-9a-f]+ dst=([0-9a-f]+)/(\d+)/([0-9a-f]+) .* size=(\d+)x(\d+)")
DISPFB = re.compile(r"dispfb1=([0-9a-f]+)")


class Draw:
    __slots__ = ("prim", "tme", "fge", "abe", "aa1", "fbp", "fbw", "fpsm", "fbmsk", "zbp", "zpsm", "zmask",
                 "tbp", "tpsm", "tw", "th", "tcc", "tfx", "cpsm", "tex1", "clamp", "alpha", "test", "fba",
                 "scissor", "aem", "pabe", "dthe", "colclamp", "rect", "vertices")

    def __init__(self, line):
        g = DRAW.search(line).groups()
        self.prim, self.tme, self.fge, self.abe, self.aa1 = (int(x) for x in g[0:5])
        self.fbp, self.fbw, self.fpsm, self.fbmsk = int(g[5], 16), int(g[6]), int(g[7], 16), int(g[8], 16)
        self.zbp, self.zpsm, self.zmask = int(g[9], 16), int(g[10], 16), int(g[11])
        self.tbp, self.tpsm, self.tw, self.th = int(g[12], 16), int(g[14], 16), int(g[15]), int(g[16])
        self.tcc, self.tfx, self.cpsm = int(g[17]), int(g[18]), int(g[19], 16)
        self.tex1, self.clamp, self.alpha, self.test, self.fba = (int(x, 16) for x in g[20:25])
        sx0, sx1, sy0, sy1 = (int(x) for x in g[25:29])
        self.scissor = (sx0, sx1, sy0, sy1)
        ofx, ofy = int(g[29]) / 16.0, int(g[30]) / 16.0
        self.aem, self.pabe, self.dthe, self.colclamp = int(g[31]), int(g[32]), int(g[33], 16), int(g[34], 16)
        points = [(float(x) - ofx, float(y) - ofy) for x, y in VERTEX.findall(line)]
        self.vertices = len(points)
        self.rect = None
        if points:
            x0, x1 = max(sx0, min(p[0] for p in points)), min(sx1, max(p[0] for p in points))
            y0, y1 = max(sy0, min(p[1] for p in points)), min(sy1, max(p[1] for p in points))
            if x1 >= x0 and y1 >= y0:
                self.rect = (x0, y0, x1, y1)

    def ztest(self):
        return (self.test >> 16) & 1


def target_pages(draw):
    """VRAM page bases (in 256-byte blocks) covered by a draw's rectangle. Pages are 64x32 pixels at 32 bpp."""
    if draw.rect is None:
        return ()
    x0, y0, x1, y1 = draw.rect
    return [draw.fbp * 32 + (py * draw.fbw + px) * 32
            for py in range(int(y0) // 32, int(y1) // 32 + 1)
            for px in range(int(x0) // 64, int(x1) // 64 + 1)]


def stream(path):
    """Yields ("field", dispfbp), ("upload", page range) and ("draw", Draw) in recording order."""
    with open(path) as handle:
        for line in handle:
            kind = line[0]
            if kind == "D":
                yield "draw", Draw(line)
            elif kind == "T":
                m = TRANSFER.match(line)
                if int(m[1]) == 0:
                    dbp, dpsm, w, h = int(m[2], 16), int(m[4], 16), int(m[5]), int(m[6])
                    blocks = int(max(w, 1) * h * BPP.get(dpsm, 4) / 256) + 1
                    yield "upload", range(dbp - dbp % 32, dbp + blocks, 32)
            elif kind == "V":
                yield "field", int(DISPFB.search(line).group(1), 16) & 0x1FF


class Pass:
    def __init__(self, key):
        self.key = key
        self.draws = self.verts = self.textured = self.blended = 0
        self.prims = collections.Counter()
        self.sources = collections.Counter()
        self.bbox = None

    def add(self, draw, source):
        self.draws += 1
        self.verts += draw.vertices
        self.prims[PRIM.get(draw.prim, str(draw.prim))] += 1
        self.blended += draw.abe
        if draw.tme:
            self.textured += 1
            self.sources[source] += 1
        if draw.rect:
            x0, y0, x1, y1 = draw.rect
            b = self.bbox or (x0, y0, x1, y1)
            self.bbox = (min(b[0], x0), min(b[1], y0), max(b[2], x1), max(b[3], y1))


def passes(path, limit):
    owner = {}
    frames = [[]]
    display = None
    in_display = False
    reads = collections.Counter()
    for kind, item in stream(path):
        if kind == "field":
            display = item
            continue
        if kind == "upload":
            for page in item:
                owner[page] = ("upload", None)
            continue
        draw = item
        if in_display and draw.fbp != display:
            if len(frames) >= limit:
                break
            frames.append([])
        in_display = draw.fbp == display
        source = None
        if draw.tme:
            writer = owner.get(draw.tbp - draw.tbp % 32)
            if writer is None:
                source = "resident"
            elif writer[0] == "upload":
                source = "upload"
            else:
                source = "%s(psm%02x) as psm%02x %dx%d" % (writer[0], writer[1], draw.tpsm, draw.tw, draw.th)
                reads[(writer[1], draw.tpsm)] += 1
        key = (draw.fbp, draw.fbw, draw.fpsm, draw.zbp if draw.ztest() or not draw.zmask else None)
        current = frames[-1]
        if not current or current[-1].key != key:
            current.append(Pass(key))
        current[-1].add(draw, source)
        for page in target_pages(draw):
            owner[page] = ("RT:%x" % draw.fbp, draw.fpsm)

    for index, frame in enumerate(frames):
        print("=== frame %d: %d passes, %d draws" % (index, len(frame), sum(p.draws for p in frame)))
        for p in frame:
            fbp, fbw, fpsm, zbp = p.key
            b = p.bbox
            area = "%dx%d@%d,%d" % (b[2] - b[0] + 1, b[3] - b[1] + 1, b[0], b[1]) if b else "-"
            rendered = sorted(s for s in p.sources if s not in ("upload", "resident"))
            plain = {s: n for s, n in p.sources.items() if s in ("upload", "resident")}
            print("  rt=%-3x w=%-2d psm=%02x z=%-4s draws=%-6d verts=%-7d %-28s tex=%-6d blend=%-6d area=%-16s %s%s"
                  % (fbp, fbw, fpsm, "%x" % zbp if zbp is not None else "-", p.draws, p.verts,
                     ",".join("%s:%d" % kv for kv in p.prims.most_common()), p.textured, p.blended, area,
                     " ".join("%s:%d" % kv for kv in sorted(plain.items())),
                     (" reads " + "; ".join(rendered)) if rendered else ""))
    print("=== render targets read back as textures (target psm -> read psm: draws)")
    for (written, read), n in reads.most_common():
        print("  psm%02x -> psm%02x: %d" % (written, read, n))


def features(paths):
    counts = collections.defaultdict(collections.Counter)
    total = 0
    for path in paths:
        for kind, d in stream(path):
            if kind != "draw":
                continue
            total += 1
            if d.abe:
                a, b, c, dd, fix = d.alpha & 3, (d.alpha >> 2) & 3, (d.alpha >> 4) & 3, (d.alpha >> 6) & 3, (d.alpha >> 32) & 0xFF
                counts["blend"]["(%s-%s)*%s%s+%s" % (BLEND_COLOR[a], BLEND_COLOR[b], BLEND_ALPHA[c],
                                                     "=%d" % fix if c == 2 else "", BLEND_COLOR[dd])] += 1
            if d.tme:
                palette = d.tpsm in (0x13, 0x14, 0x1B, 0x24, 0x2C)
                counts["texture"]["psm%02x%s" % (d.tpsm, "/clut%02x" % d.cpsm if palette else "")] += 1
                counts["tfx"]["tfx%d tcc%d" % (d.tfx, d.tcc)] += 1
                counts["filter"]["mag%s mmin%d" % ("lin" if (d.tex1 >> 5) & 1 else "near", (d.tex1 >> 6) & 7)] += 1
                counts["wrap"]["wms%d wmt%d" % (d.clamp & 3, (d.clamp >> 2) & 3)] += 1
                counts["texa"]["aem%d" % d.aem] += 1
            t = d.test
            counts["alpha test"]["off" if not t & 1 else "atst%d ref%d afail%d" % ((t >> 1) & 7, (t >> 4) & 0xFF, (t >> 12) & 3)] += 1
            counts["dest alpha"]["date%d datm%d" % ((t >> 14) & 1, (t >> 15) & 1)] += 1
            counts["depth"]["zte%d ztst%d zmask%d" % ((t >> 16) & 1, (t >> 17) & 3, d.zmask)] += 1
            counts["formats"]["rt psm%02x z psm%02x" % (d.fpsm, d.zpsm)] += 1
            counts["fbmsk"]["%08x" % d.fbmsk] += 1
            counts["misc"]["fge%d" % d.fge] += 1
            counts["misc"]["aa1=%d" % d.aa1] += 1
            counts["misc"]["pabe%d" % d.pabe] += 1
            counts["misc"]["fba%d" % (d.fba & 1)] += 1
            counts["misc"]["dthe%d" % (d.dthe & 1)] += 1
            counts["misc"]["colclamp%d" % (d.colclamp & 1)] += 1
    print("draws: %d" % total)
    for name, counter in counts.items():
        print("%-11s %s" % (name, ", ".join("%s: %d" % kv for kv in counter.most_common(14))))


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "passes":
        passes(sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 3)
    elif len(sys.argv) >= 3 and sys.argv[1] == "features":
        features(sys.argv[2:])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
