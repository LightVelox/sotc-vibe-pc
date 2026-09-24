import argparse
import struct

from PIL import Image

BLOCK32 = [[0, 1, 4, 5, 16, 17, 20, 21], [2, 3, 6, 7, 18, 19, 22, 23], [8, 9, 12, 13, 24, 25, 28, 29], [10, 11, 14, 15, 26, 27, 30, 31]]
COL32 = [[0, 1, 4, 5, 8, 9, 12, 13], [2, 3, 6, 7, 10, 11, 14, 15], [16, 17, 20, 21, 24, 25, 28, 29], [18, 19, 22, 23, 26, 27, 30, 31],
         [32, 33, 36, 37, 40, 41, 44, 45], [34, 35, 38, 39, 42, 43, 46, 47], [48, 49, 52, 53, 56, 57, 60, 61], [50, 51, 54, 55, 58, 59, 62, 63]]


BLOCK4 = [
    [0, 2, 8, 10],
    [1, 3, 9, 11],
    [4, 6, 12, 14],
    [5, 7, 13, 15],
    [16, 18, 24, 26],
    [17, 19, 25, 27],
    [20, 22, 28, 30],
    [21, 23, 29, 31],
]

COL4 = [
    [0, 8, 32, 40, 64, 72, 96, 104, 2, 10, 34, 42, 66, 74, 98, 106,
     4, 12, 36, 44, 68, 76, 100, 108, 6, 14, 38, 46, 70, 78, 102, 110],
    [16, 24, 48, 56, 80, 88, 112, 120, 18, 26, 50, 58, 82, 90, 114, 122,
     20, 28, 52, 60, 84, 92, 116, 124, 22, 30, 54, 62, 86, 94, 118, 126],
    [65, 73, 97, 105, 1, 9, 33, 41, 67, 75, 99, 107, 3, 11, 35, 43,
     69, 77, 101, 109, 5, 13, 37, 45, 71, 79, 103, 111, 7, 15, 39, 47],
    [81, 89, 113, 121, 17, 25, 49, 57, 83, 91, 115, 123, 19, 27, 51, 59,
     85, 93, 117, 125, 21, 29, 53, 61, 87, 95, 119, 127, 23, 31, 55, 63],
    [192, 200, 224, 232, 128, 136, 160, 168, 194, 202, 226, 234, 130, 138, 162, 170,
     196, 204, 228, 236, 132, 140, 164, 172, 198, 206, 230, 238, 134, 142, 166, 174],
    [208, 216, 240, 248, 144, 152, 176, 184, 210, 218, 242, 250, 146, 154, 178, 186,
     212, 220, 244, 252, 148, 156, 180, 188, 214, 222, 246, 254, 150, 158, 182, 190],
    [129, 137, 161, 169, 193, 201, 225, 233, 131, 139, 163, 171, 195, 203, 227, 235,
     133, 141, 165, 173, 197, 205, 229, 237, 135, 143, 167, 175, 199, 207, 231, 239],
    [145, 153, 177, 185, 209, 217, 241, 249, 147, 155, 179, 187, 211, 219, 243, 251,
     149, 157, 181, 189, 213, 221, 245, 253, 151, 159, 183, 191, 215, 223, 247, 255],
]
COL4 += [[v + 256 for v in row] for row in COL4]


def read32(vram, bp, bw, x, y):
    page = (y >> 5) * bw + (x >> 6)
    block = bp + page * 32 + BLOCK32[(y >> 3) & 3][(x >> 3) & 7]
    word = COL32[y & 7][x & 7]
    return struct.unpack_from("<I", vram, (block * 256 + word * 4) % len(vram))[0]



def read4(vram, bp, bw, x, y):
    page = (y >> 7) * (bw >> 1) + (x >> 7)
    block = bp + page * 32 + BLOCK4[(y >> 4) & 7][(x >> 5) & 3]
    nib = COL4[y & 15][x & 31]
    byte = vram[(block * 256 + nib // 2) % len(vram)]
    return (byte >> 4) if nib & 1 else (byte & 15)



def main():
    ap = argparse.ArgumentParser(description="Decode a region of a raw 4 MiB GS VRAM dump (PS2X_GS_TRACE .vram or gsdump.py --vram)")
    ap.add_argument("vram")
    ap.add_argument("psm", choices=["ct32", "t4"])
    ap.add_argument("base", type=lambda v: int(v, 16), help="block address (TBP/FBP*32), hex")
    ap.add_argument("bw", type=int, help="buffer width in 64-pixel units")
    ap.add_argument("width", type=int)
    ap.add_argument("height", type=int)
    ap.add_argument("out")
    args = ap.parse_args()
    vram = open(args.vram, "rb").read()
    if args.psm == "ct32":
        im = Image.new("RGB", (args.width, args.height))
        im.putdata([(lambda c: (c & 255, (c >> 8) & 255, (c >> 16) & 255))(read32(vram, args.base, args.bw, x, y))
                    for y in range(args.height) for x in range(args.width)])
    else:
        im = Image.new("L", (args.width, args.height))
        im.putdata([read4(vram, args.base, args.bw, x, y) * 17 for y in range(args.height) for x in range(args.width)])
    im.save(args.out)


if __name__ == "__main__":
    main()
