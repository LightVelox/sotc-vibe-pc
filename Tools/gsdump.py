import argparse
import struct
import sys

PRIM, RGBAQ, ST, UV, XYZF2, XYZ2, TEX0_1, TEX0_2, CLAMP_1, CLAMP_2, FOG = range(0x0B)
XYZF3, XYZ3 = 0x0C, 0x0D
TEX1_1, TEX1_2 = 0x14, 0x15
XYOFFSET_1, XYOFFSET_2 = 0x18, 0x19
PRMODECONT, PRMODE, TEXCLUT, SCANMSK = 0x1A, 0x1B, 0x1C, 0x22
TEXA, FOGCOL = 0x3B, 0x3D
SCISSOR_1, SCISSOR_2, ALPHA_1, ALPHA_2 = 0x40, 0x41, 0x42, 0x43
DIMX, DTHE, COLCLAMP, TEST_1, TEST_2, PABE, FBA_1, FBA_2 = 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B
FRAME_1, FRAME_2, ZBUF_1, ZBUF_2 = 0x4C, 0x4D, 0x4E, 0x4F
BITBLTBUF, TRXPOS, TRXREG, TRXDIR = 0x50, 0x51, 0x52, 0x53

CTX_REGS = {
    TEX0_1: "TEX0", TEX0_2: "TEX0", CLAMP_1: "CLAMP", CLAMP_2: "CLAMP", TEX1_1: "TEX1", TEX1_2: "TEX1",
    XYOFFSET_1: "XYOFFSET", XYOFFSET_2: "XYOFFSET", SCISSOR_1: "SCISSOR", SCISSOR_2: "SCISSOR",
    ALPHA_1: "ALPHA", ALPHA_2: "ALPHA", TEST_1: "TEST", TEST_2: "TEST", FBA_1: "FBA", FBA_2: "FBA",
    FRAME_1: "FRAME", FRAME_2: "FRAME", ZBUF_1: "ZBUF", ZBUF_2: "ZBUF",
}
CTX_ORDER = ["XYOFFSET", "TEX0", "TEX1", "CLAMP", "MIPTBP1", "MIPTBP2", "SCISSOR", "ALPHA", "TEST", "FBA", "FRAME", "ZBUF"]
VRAM_OFFSET_BY_STATE_VERSION = {9: 0x1A9}
NEEDED = {0: 1, 1: 2, 2: 2, 3: 3, 4: 3, 5: 3, 6: 2}


def bits(v, lo, n):
    return (v >> lo) & ((1 << n) - 1)


class Gs:
    def __init__(self, out):
        self.out = out
        self.env = {}
        self.ctx = [dict.fromkeys(CTX_ORDER, 0), dict.fromkeys(CTX_ORDER, 0)]
        self.rgbaq = 0
        self.st = 0
        self.uv = 0
        self.fog = 0
        self.queue = []
        self.prim = 0

    def load_state(self, state):
        o = 4
        names = ["PRIM", "PRMODECONT", "TEXCLUT", "SCANMSK", "TEXA", "FOGCOL", "DIMX", "DTHE", "COLCLAMP", "PABE",
                 "BITBLTBUF", "TRXDIR", "TRXPOS", "TRXREG", "TRXREG_OLD"]
        for n in names:
            self.env[n] = struct.unpack_from("<Q", state, o)[0]
            o += 8
        for c in range(2):
            for n in CTX_ORDER:
                self.ctx[c][n] = struct.unpack_from("<Q", state, o)[0]
                o += 8
        self.rgbaq, self.st, self.uv, self.fog = struct.unpack_from("<4Q", state, o)
        self.prim = self.env["PRIM"]
        return VRAM_OFFSET_BY_STATE_VERSION[struct.unpack_from("<I", state, 0)[0]]

    def prim_attrs(self):
        if bits(self.env.get("PRMODECONT", 1), 0, 1):
            return self.prim
        return (self.prim & 7) | (self.env.get("PRMODE", 0) & ~7)

    def write(self, reg, v):
        if reg == PRIM:
            self.prim = v
            self.queue = []
        elif reg == RGBAQ:
            self.rgbaq = v
        elif reg == ST:
            self.st = v
        elif reg == UV:
            self.uv = v
        elif reg == FOG:
            self.fog = v
        elif reg in (XYZF2, XYZ2, XYZF3, XYZ3):
            x, y = bits(v, 0, 16), bits(v, 16, 16)
            z = bits(v, 32, 24) if reg in (XYZF2, XYZF3) else bits(v, 32, 32)
            self.kick(x, y, z, reg in (XYZF2, XYZ2))
        elif reg in CTX_REGS:
            c = 0 if reg in (TEX0_1, CLAMP_1, TEX1_1, XYOFFSET_1, SCISSOR_1, ALPHA_1, TEST_1, FBA_1, FRAME_1, ZBUF_1) else 1
            self.ctx[c][CTX_REGS[reg]] = v
        elif reg == TRXDIR:
            self.env["TRXDIR"] = v
            b, pos, r = self.env.get("BITBLTBUF", 0), self.env.get("TRXPOS", 0), self.env.get("TRXREG", 0)
            self.out.write("T dir=%u src=%x/%u/%02x dst=%x/%u/%02x ss=%u,%u ds=%u,%u size=%ux%u\n" % (
                v & 3, bits(b, 0, 14), bits(b, 16, 6), bits(b, 24, 6), bits(b, 32, 14), bits(b, 48, 6), bits(b, 56, 6),
                bits(pos, 0, 11), bits(pos, 16, 11), bits(pos, 32, 11), bits(pos, 48, 11), bits(r, 0, 12), bits(r, 32, 12)))
        else:
            name = {PRMODECONT: "PRMODECONT", PRMODE: "PRMODE", TEXCLUT: "TEXCLUT", SCANMSK: "SCANMSK", TEXA: "TEXA",
                    FOGCOL: "FOGCOL", DIMX: "DIMX", DTHE: "DTHE", COLCLAMP: "COLCLAMP", PABE: "PABE",
                    BITBLTBUF: "BITBLTBUF", TRXPOS: "TRXPOS", TRXREG: "TRXREG"}.get(reg)
            if name:
                self.env[name] = v

    def kick(self, x, y, z, draw):
        p = self.prim_attrs()
        vert = (x, y, z, self.rgbaq, self.st, self.uv)
        self.queue.append(vert)
        t = p & 7
        need = NEEDED.get(t, 0)
        if not need or len(self.queue) < need:
            return
        if draw:
            self.emit(p, self.queue[:need])
        if t in (0, 1, 3, 6):
            self.queue = []
        elif t == 2:
            self.queue = self.queue[1:2]
        elif t == 4:
            self.queue = self.queue[1:3]
        elif t == 5:
            self.queue = [self.queue[0], self.queue[2]]

    def emit(self, p, verts):
        ci = bits(p, 9, 1)
        c = self.ctx[ci]
        fr, zb, t0 = c["FRAME"], c["ZBUF"], c["TEX0"]
        sc, ofs = c["SCISSOR"], c["XYOFFSET"]
        texa = self.env.get("TEXA", 0)
        line = ("D prim=%u iip=%d tme=%d fge=%d abe=%d aa1=%d fst=%d ctxt=%d fix=%d"
                " frame=%x/%u/%02x/%08x zbuf=%x/%02x/%d"
                " tex0=%x/%u/%02x/%ux%u/tcc%u/tfx%u/cbp%x/cpsm%02x/csm%u/csa%u/cld%u"
                " tex1=%x clamp=%x alpha=%x test=%x fba=%x scissor=%u-%u,%u-%u ofs=%u,%u"
                " texa=%u/%d/%u pabe=%d dthe=%x colclamp=%x") % (
            p & 7, bits(p, 3, 1), bits(p, 4, 1), bits(p, 5, 1), bits(p, 6, 1), bits(p, 7, 1), bits(p, 8, 1), ci, bits(p, 10, 1),
            bits(fr, 0, 9), bits(fr, 16, 6), bits(fr, 24, 6), bits(fr, 32, 32), bits(zb, 0, 9), bits(zb, 24, 4) | 0x30, bits(zb, 32, 1),
            bits(t0, 0, 14), bits(t0, 14, 6), bits(t0, 20, 6), 1 << bits(t0, 26, 4), 1 << bits(t0, 30, 4), bits(t0, 34, 1), bits(t0, 35, 2),
            bits(t0, 37, 14), bits(t0, 51, 4), bits(t0, 55, 1), bits(t0, 56, 5), bits(t0, 61, 3),
            c["TEX1"], c["CLAMP"], c["ALPHA"], c["TEST"], c["FBA"],
            bits(sc, 0, 11), bits(sc, 16, 11), bits(sc, 32, 11), bits(sc, 48, 11), bits(ofs, 0, 16), bits(ofs, 32, 16),
            bits(texa, 0, 8), bits(texa, 15, 1), bits(texa, 32, 8), self.env.get("PABE", 0) & 1, self.env.get("DTHE", 0),
            self.env.get("COLCLAMP", 0))
        for x, y, z, rgbaq, st, uv in verts:
            s = struct.unpack("<f", struct.pack("<I", st & 0xFFFFFFFF))[0]
            tt = struct.unpack("<f", struct.pack("<I", st >> 32))[0]
            q = struct.unpack("<f", struct.pack("<I", rgbaq >> 32))[0]
            line += " | %.2f,%.2f z=%d rgba=%02x%02x%02x%02x uv=%.2f,%.2f st=%g,%g q=%g" % (
                x / 16, y / 16, z, bits(rgbaq, 0, 8), bits(rgbaq, 8, 8), bits(rgbaq, 16, 8), bits(rgbaq, 24, 8),
                bits(uv, 0, 14) / 16, bits(uv, 16, 14) / 16, s, tt, q)
        self.out.write(line + "\n")

    def packed(self, desc, lo, hi):
        if desc == PRIM:
            self.write(PRIM, lo & 0x7FF)
        elif desc == RGBAQ:
            q = self.rgbaq >> 32
            self.rgbaq = bits(lo, 0, 8) | bits(lo, 32, 8) << 8 | bits(hi, 0, 8) << 16 | bits(hi, 32, 8) << 24 | q << 32
        elif desc == ST:
            self.st = lo
            self.rgbaq = (self.rgbaq & 0xFFFFFFFF) | (hi & 0xFFFFFFFF) << 32
        elif desc == UV:
            self.uv = bits(lo, 0, 14) | bits(lo, 32, 14) << 16
        elif desc in (XYZF2, XYZF3):
            v = bits(lo, 0, 16) | bits(lo, 32, 16) << 16 | bits(hi, 4, 24) << 32 | bits(hi, 36, 8) << 56
            reg = XYZF3 if desc == XYZF3 or bits(hi, 47, 1) else XYZF2
            self.write(reg, v)
        elif desc in (XYZ2, XYZ3):
            v = bits(lo, 0, 16) | bits(lo, 32, 16) << 16 | (hi & 0xFFFFFFFF) << 32
            reg = XYZ3 if desc == XYZ3 or bits(hi, 47, 1) else XYZ2
            self.write(reg, v)
        elif desc == FOG:
            self.fog = bits(hi, 36, 8) << 56
        elif desc == 0x0E:
            self.write(hi & 0xFF, lo)
        elif desc == 0x0F:
            pass
        else:
            self.write(desc, lo)

    def gif(self, data):
        o = 0
        while o + 16 <= len(data):
            lo, hi = struct.unpack_from("<QQ", data, o)
            o += 16
            nloop, pre, prim, flg, nreg = bits(lo, 0, 15), bits(lo, 46, 1), bits(lo, 47, 11), bits(lo, 58, 2), bits(lo, 60, 4) or 16
            regs = [bits(hi, 4 * i, 4) for i in range(nreg)]
            if pre and flg == 0:
                self.write(PRIM, prim)
            if flg == 0:
                for _ in range(nloop):
                    for r in regs:
                        if o + 16 > len(data):
                            return
                        a, b = struct.unpack_from("<QQ", data, o)
                        o += 16
                        self.packed(r, a, b)
            elif flg == 1:
                count = nloop * nreg
                for i in range(count):
                    v = struct.unpack_from("<Q", data, o + 8 * i)[0]
                    self.write(regs[i % nreg], v)
                o += ((count + 1) // 2) * 16
            else:
                o += nloop * 16


def main():
    ap = argparse.ArgumentParser(description="Decode a PCSX2 GS dump into the runtime's PS2X_GS_TRACE format")
    ap.add_argument("dump")
    ap.add_argument("--out", default="-")
    ap.add_argument("--vram")
    ap.add_argument("--screenshot")
    args = ap.parse_args()
    d = open(args.dump, "rb").read()
    magic, header_size = struct.unpack_from("<II", d, 0)
    if magic != 0xFFFFFFFF:
        sys.exit("old-format GS dump is not supported")
    version, state_size, serial_off, serial_size, crc, sw, sh, shot_off, shot_size = struct.unpack_from("<9I", d, 8)
    base = 8
    if args.screenshot and shot_size:
        import zlib
        raw = d[base + shot_off:base + shot_off + shot_size]
        rows = b"".join(b"\0" + raw[y * sw * 4:(y + 1) * sw * 4] for y in range(sh))
        chunk = lambda t, b: struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)
        png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", sw, sh, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
        open(args.screenshot, "wb").write(png)
    o = base + header_size
    state = d[o:o + state_size]
    o += state_size
    regs = d[o:o + 8192]
    o += 8192
    out = sys.stdout if args.out == "-" else open(args.out, "w")
    gs = Gs(out)
    vram_off = gs.load_state(state)
    if args.vram:
        open(args.vram, "wb").write(state[vram_off:vram_off + 4 * 1024 * 1024])
    tick = 0

    def vline(r):
        pmode, smode2 = struct.unpack_from("<Q", r, 0)[0], struct.unpack_from("<Q", r, 0x20)[0]
        dispfb1, display1, dispfb2, display2 = struct.unpack_from("<Q", r, 0x70)[0], struct.unpack_from("<Q", r, 0x80)[0], struct.unpack_from("<Q", r, 0x90)[0], struct.unpack_from("<Q", r, 0xA0)[0]
        bg = struct.unpack_from("<Q", r, 0xE0)[0]
        return "V tick=%u pmode=%x smode2=%x dispfb1=%x display1=%x dispfb2=%x display2=%x bgcolor=%x\n" % (
            tick, pmode, smode2, dispfb1, display1, dispfb2, display2, bg)

    out.write(vline(regs))
    while o < len(d):
        kind = d[o]
        o += 1
        if kind == 0:
            path = d[o]
            size = struct.unpack_from("<I", d, o + 1)[0]
            o += 5
            gs.gif(d[o:o + size])
            o += size
        elif kind == 1:
            o += 1
            tick += 1
            out.write(vline(regs))
        elif kind == 2:
            o += 4
        elif kind == 3:
            regs = d[o:o + 8192]
            o += 8192
        else:
            sys.exit("unknown packet %d at %d" % (kind, o - 1))


if __name__ == "__main__":
    main()
