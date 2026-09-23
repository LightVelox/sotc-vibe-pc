import argparse
import socket
import struct
import sys
import time

MSG_READ8 = 0
MSG_READ32 = 2
MSG_READ64 = 3
MSG_WRITE8 = 4
MSG_WRITE32 = 6
MSG_VERSION = 8
MSG_SAVE_STATE = 9
MSG_LOAD_STATE = 10
MSG_TITLE = 11
MSG_ID = 12
MSG_UUID = 13
MSG_GAME_VERSION = 14
MSG_STATUS = 15

STATUS_NAMES = {0: "running", 1: "paused", 2: "shutdown"}


class Pine:
    def __init__(self, port=28011, timeout=10.0):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=timeout)

    def close(self):
        self.sock.close()

    def _recv_exact(self, n):
        buf = bytearray()
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("PINE connection closed")
            buf += chunk
        return bytes(buf)

    def _transact(self, payload):
        self.sock.sendall(struct.pack("<I", len(payload) + 4) + payload)
        size = struct.unpack("<I", self._recv_exact(4))[0]
        body = self._recv_exact(size - 4)
        if body[0] != 0:
            raise RuntimeError("PINE command failed")
        return body[1:]

    def _string(self, op):
        data = self._transact(bytes([op]))
        n = struct.unpack_from("<I", data, 0)[0]
        return data[4:4 + n].rstrip(b"\0").decode("utf-8", "replace")

    def version(self):
        return self._string(MSG_VERSION)

    def title(self):
        return self._string(MSG_TITLE)

    def game_id(self):
        return self._string(MSG_ID)

    def game_version(self):
        return self._string(MSG_GAME_VERSION)

    def status(self):
        data = self._transact(bytes([MSG_STATUS]))
        return STATUS_NAMES.get(struct.unpack("<I", data)[0], "unknown")

    def read32(self, addr):
        return struct.unpack("<I", self._transact(struct.pack("<BI", MSG_READ32, addr)))[0]

    def try_read32(self, addr):
        try:
            return self.read32(addr)
        except RuntimeError:
            return None

    def write32(self, addr, value):
        self._transact(struct.pack("<BII", MSG_WRITE32, addr, value))

    def read_block(self, addr, size, batch=16384):
        assert addr % 8 == 0 and size % 8 == 0
        out = bytearray()
        pos = addr
        end = addr + size
        while pos < end:
            n = min(batch, (end - pos) // 8)
            payload = b"".join(struct.pack("<BI", MSG_READ64, pos + 8 * i) for i in range(n))
            out += self._transact(payload)
            pos += 8 * n
        return bytes(out)

    def save_state(self, slot):
        self._transact(struct.pack("<BB", MSG_SAVE_STATE, slot))

    def load_state(self, slot):
        self._transact(struct.pack("<BB", MSG_LOAD_STATE, slot))


def main():
    ap = argparse.ArgumentParser(description="PCSX2 PINE IPC client")
    ap.add_argument("--port", type=int, default=28011)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("info")
    r = sub.add_parser("read32")
    r.add_argument("addr", type=lambda x: int(x, 0))
    r.add_argument("count", type=lambda x: int(x, 0), nargs="?", default=1)
    d = sub.add_parser("dump")
    d.add_argument("addr", type=lambda x: int(x, 0))
    d.add_argument("size", type=lambda x: int(x, 0))
    d.add_argument("out")
    t = sub.add_parser("trap", help="park the EE at an address by writing 'b .' there, dump RAM, restore")
    t.add_argument("addr", type=lambda x: int(x, 0))
    t.add_argument("out")
    t.add_argument("--settle", type=float, default=2.0)
    t.add_argument("--keep", action="store_true", help="leave the trap in place")
    t.add_argument("--wait-for", type=lambda x: int(x, 0), help="poll until this word is at addr before trapping")
    t.add_argument("--timeout", type=float, default=60.0)
    t.add_argument("--wait-nonzero", type=lambda x: int(x, 0), help="poll until this address holds a non-zero word")
    w = sub.add_parser("wait")
    w.add_argument("seconds", type=float, nargs="?", default=60)
    args = ap.parse_args()

    if args.cmd == "wait":
        deadline = time.time() + args.seconds
        while time.time() < deadline:
            try:
                p = Pine(args.port, timeout=2)
                print(p.status())
                return
            except OSError:
                time.sleep(0.5)
        sys.exit("PINE not reachable")

    p = Pine(args.port)
    if args.cmd == "info":
        print("version:", p.version())
        print("status:", p.status())
        for name, fn in (("title", p.title), ("id", p.game_id), ("game_version", p.game_version)):
            try:
                print(f"{name}:", fn())
            except RuntimeError:
                print(f"{name}: <unavailable>")
    elif args.cmd == "read32":
        for i in range(args.count):
            a = args.addr + 4 * i
            print(f"{a:08x}: {p.read32(a):08x}")
    elif args.cmd == "trap":
        deadline = time.time() + args.timeout
        if args.wait_nonzero is not None:
            while not p.try_read32(args.wait_nonzero):
                if time.time() > deadline:
                    sys.exit(f"timed out waiting for a non-zero word at {args.wait_nonzero:08x}")
                time.sleep(0.002)
        if args.wait_for is not None:
            while p.try_read32(args.addr) != args.wait_for:
                if time.time() > deadline:
                    sys.exit(f"timed out waiting for {args.wait_for:08x} at {args.addr:08x}")
                time.sleep(0.001)
        original = p.read32(args.addr)
        p.write32(args.addr, 0x1000FFFF)
        print(f"trap set at {args.addr:08x} (original {original:08x}); waiting {args.settle}s")
        time.sleep(args.settle)
        still = p.read32(args.addr)
        data = p.read_block(0, 0x2000000)
        open(args.out, "wb").write(data)
        if not args.keep:
            p.write32(args.addr, original)
        print(f"dumped RAM to {args.out}; trap word now {still:08x}; {'kept' if args.keep else 'restored'}")
    elif args.cmd == "dump":
        t = time.time()
        data = p.read_block(args.addr, args.size)
        open(args.out, "wb").write(data)
        print(f"dumped {len(data)} bytes in {time.time() - t:.1f}s -> {args.out}")


if __name__ == "__main__":
    main()
