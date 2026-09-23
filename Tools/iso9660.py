import argparse
import hashlib
import os
import struct
import sys

SECTOR = 2048


class Entry:
    def __init__(self, path, lba, size, is_dir):
        self.path = path
        self.lba = lba
        self.size = size
        self.is_dir = is_dir


class Iso9660:
    def __init__(self, path):
        self.path = path
        self.f = open(path, "rb")
        self.pvd = self._read_pvd()

    def read_sectors(self, lba, count):
        self.f.seek(lba * SECTOR)
        return self.f.read(count * SECTOR)

    def read_extent(self, lba, size):
        self.f.seek(lba * SECTOR)
        return self.f.read(size)

    def _read_pvd(self):
        lba = 16
        while True:
            d = self.read_sectors(lba, 1)
            if d[1:6] != b"CD001":
                raise ValueError("not an ISO9660 image")
            if d[0] == 1:
                return d
            if d[0] == 255:
                raise ValueError("no primary volume descriptor")
            lba += 1

    def info(self):
        d = self.pvd
        return {
            "system_id": d[8:40].decode("ascii", "replace").strip(),
            "volume_id": d[40:72].decode("ascii", "replace").strip(),
            "volume_space_size": struct.unpack_from("<I", d, 80)[0],
            "logical_block_size": struct.unpack_from("<H", d, 128)[0],
            "volume_set_id": d[190:318].decode("ascii", "replace").strip(),
            "publisher_id": d[318:446].decode("ascii", "replace").strip(),
            "preparer_id": d[446:574].decode("ascii", "replace").strip(),
            "application_id": d[574:702].decode("ascii", "replace").strip(),
            "creation_date": d[813:830].decode("ascii", "replace"),
            "modification_date": d[830:847].decode("ascii", "replace"),
        }

    def _parse_dir(self, lba, size, prefix, out):
        data = self.read_extent(lba, size)
        off = 0
        while off < len(data):
            rlen = data[off]
            if rlen == 0:
                off = (off // SECTOR + 1) * SECTOR
                continue
            rec = data[off:off + rlen]
            elba = struct.unpack_from("<I", rec, 2)[0]
            esize = struct.unpack_from("<I", rec, 10)[0]
            flags = rec[25]
            nlen = rec[32]
            name = rec[33:33 + nlen]
            off += rlen
            if name in (b"\x00", b"\x01"):
                continue
            n = name.decode("ascii", "replace")
            is_dir = bool(flags & 2)
            full = prefix + "/" + n
            out.append(Entry(full, elba, esize, is_dir))
            if is_dir:
                self._parse_dir(elba, esize, full, out)

    def walk(self):
        root = self.pvd[156:156 + 34]
        lba = struct.unpack_from("<I", root, 2)[0]
        size = struct.unpack_from("<I", root, 10)[0]
        out = []
        self._parse_dir(lba, size, "", out)
        return out

    def find(self, name):
        want = name.upper().lstrip("/")
        for e in self.walk():
            p = e.path.lstrip("/").upper()
            if p == want or p.split(";")[0] == want:
                return e
        return None

    def extract(self, entry, dest):
        os.makedirs(os.path.dirname(dest) or ".", exist_ok=True)
        remaining = entry.size
        self.f.seek(entry.lba * SECTOR)
        with open(dest, "wb") as o:
            while remaining:
                chunk = self.f.read(min(remaining, 1 << 22))
                o.write(chunk)
                remaining -= len(chunk)


def main():
    ap = argparse.ArgumentParser(description="Minimal ISO9660 reader for PS2 DVD images")
    ap.add_argument("iso")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("info")
    sub.add_parser("ls")
    x = sub.add_parser("extract")
    x.add_argument("name")
    x.add_argument("dest")
    xa = sub.add_parser("extract-all")
    xa.add_argument("dest")
    xa.add_argument("--max-size", type=int, default=0)
    args = ap.parse_args()

    iso = Iso9660(args.iso)
    if args.cmd == "info":
        for k, v in iso.info().items():
            print(f"{k}: {v}")
    elif args.cmd == "ls":
        for e in iso.walk():
            kind = "D" if e.is_dir else "F"
            print(f"{kind} lba={e.lba:8d} size={e.size:11d} {e.path}")
    elif args.cmd == "extract":
        e = iso.find(args.name)
        if not e:
            sys.exit(f"not found: {args.name}")
        iso.extract(e, args.dest)
        print(f"extracted {e.path} ({e.size} bytes) -> {args.dest}")
    elif args.cmd == "extract-all":
        for e in iso.walk():
            if e.is_dir:
                continue
            if args.max_size and e.size > args.max_size:
                continue
            rel = e.path.lstrip("/").split(";")[0]
            iso.extract(e, os.path.join(args.dest, rel))
            print(f"{e.size:11d} {rel}")


if __name__ == "__main__":
    main()
