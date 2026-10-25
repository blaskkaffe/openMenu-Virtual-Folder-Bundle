#!/usr/bin/env python3
"""ppm2png.py in.ppm out.png: PPM (P6) to PNG with the standard library only.
ppm2png.py --grid out.png a.ppm b.ppm c.ppm d.ppm: the four pictures side by side, two by two, each at half size."""
import struct
import sys
import zlib


def read(src):
    data = open(src, "rb").read()
    parts = data.split(b"\n", 3)
    assert parts[0] == b"P6"
    w, h = map(int, parts[1].split())
    return w, h, parts[3]


def half(w, h, pix):
    out = bytearray()
    for y in range(0, h - 1, 2):
        a, b = y * w * 3, (y + 1) * w * 3
        for x in range(0, w - 1, 2):
            for c in range(3):
                i = x * 3 + c
                out.append((pix[a + i] + pix[a + i + 3] + pix[b + i] + pix[b + i + 3] + 2) // 4)
    return w // 2, h // 2, bytes(out)


def grid(dst, srcs):
    tiles = [half(*read(s)) for s in srcs]
    w, h = tiles[0][0], tiles[0][1]
    pix = bytearray()
    for row in range(2):
        for y in range(h):
            for col in range(2):
                t = tiles[row * 2 + col]
                pix += t[2][y * w * 3:(y + 1) * w * 3]
    write(dst, w * 2, h * 2, bytes(pix))


def main(src, dst):
    w, h, pix = read(src)
    write(dst, w, h, pix)


def write(dst, w, h, pix):
    rows = b"".join(b"\x00" + pix[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(kind, body):
        crc = zlib.crc32(kind + body) & 0xFFFFFFFF
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", crc)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) \
        + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b"")
    open(dst, "wb").write(png)


if __name__ == "__main__":
    if sys.argv[1] == "--grid":
        grid(sys.argv[2], sys.argv[3:7])
    else:
        main(sys.argv[1], sys.argv[2])
