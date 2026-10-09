#!/usr/bin/env python3
"""Replace the "Dreamcast" logo in the BIOS-style menu header.

The launcher looks for LOGO.PVR next to OPENMENU.INI (the root of the openMenu disc image).
If it is there, the picture replaces the logo; if not, the logo from the console's BIOS is used.

  biologo.py extract dc_boot.bin logo.png   save the BIOS logo as PNG, as a starting point
  biologo.py convert logo.png LOGO.PVR      make LOGO.PVR from a PNG

The logo area is 4:1; the picture is resized to 128x32 (transparent areas stay transparent).
Needs Pillow (pip install pillow). Extract needs a boot ROM dump of your own.
"""
import struct
import sys

from PIL import Image

W, H = 128, 32


def to_argb4444(img):
    out = bytearray()
    for r, g, b, a in img.convert("RGBA").get_flattened_data() if hasattr(img, "get_flattened_data") else img.convert("RGBA").getdata():
        v = ((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)
        out += struct.pack("<H", v)
    return bytes(out)


def convert(src, dst):
    img = Image.open(src).convert("RGBA").resize((W, H), Image.LANCZOS)
    # GBIX header (index 0) + PVRT: pixel format 2 = ARGB4444, data type 9 = rectangle
    payload = to_argb4444(img)
    pvrt = b"PVRT" + struct.pack("<I", len(payload) + 8) + bytes([2, 9, 0, 0]) + struct.pack("<HH", W, H)
    gbix = b"GBIX" + struct.pack("<II", 8, 0) + b"\0" * 4
    with open(dst, "wb") as f:
        f.write(gbix + pvrt + payload)
    print("wrote", dst, "(%d bytes)" % (len(gbix) + len(pvrt) + len(payload)))


def extract(rom, dst):
    d = open(rom, "rb").read()
    off = 0x7C970  # GBIX 114, the logo of the header bar (menu image of BIOS 1.01c/d, 1.022, 1.032)
    if d[off:off + 4] != b"GBIX":
        sys.exit("this does not look like a supported boot ROM")
    p = d.find(b"PVRT", off, off + 0x20)
    fmt, typ = d[p + 8], d[p + 9]
    w, h = struct.unpack_from("<HH", d, p + 12)
    if typ != 9:
        sys.exit("unexpected logo format %d" % typ)
    data = p + 16
    img = Image.new("RGBA", (w, h))
    for i in range(w * h):
        v = struct.unpack_from("<H", d, data + 2 * i)[0]
        if fmt == 2:
            a, r, g, b = ((v >> 12) & 15) * 17, ((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17
        elif fmt == 1:
            a, r, g, b = 255, ((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31
        else:
            a, r, g, b = (255 if v & 0x8000 else 0), ((v >> 10) & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31, (v & 31) * 255 // 31
        img.putpixel((i % w, i // w), (r, g, b, a))
    img.save(dst)
    print("wrote", dst, "%dx%d" % (w, h))


if __name__ == "__main__" and len(sys.argv) == 4 and sys.argv[1] in ("extract", "convert"):
    (extract if sys.argv[1] == "extract" else convert)(sys.argv[2], sys.argv[3])
else:
    print(__doc__)
    sys.exit(2)
