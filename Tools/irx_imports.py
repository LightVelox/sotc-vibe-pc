import glob
import struct
import sys


def imports(path):
    d = open(path, "rb").read()
    result = []
    for off in range(0, len(d) - 20, 4):
        if struct.unpack_from("<I", d, off)[0] != 0x41E00000:
            continue
        version = struct.unpack_from("<I", d, off + 8)[0]
        name = d[off + 12:off + 20].split(b"\0")[0].decode("latin1")
        ordinals = []
        p = off + 20
        while p + 8 <= len(d):
            a, b = struct.unpack_from("<II", d, p)
            if a == 0x03E00008 and (b >> 16) == 0x2400:
                ordinals.append(b & 0xFFFF)
                p += 8
            else:
                break
        result.append((name, version, ordinals))
    return result


if __name__ == "__main__":
    for path in sys.argv[1:]:
        print(path)
        for name, version, ordinals in imports(path):
            print(f"    {name:8s} v{version >> 8}.{version & 0xFF:02x} ordinals {ordinals}")
