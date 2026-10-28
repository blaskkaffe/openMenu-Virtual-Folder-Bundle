#!/usr/bin/env python3
"""Make PS2.PAK, the asset pack of the PS2-style menu, from a PS2 BIOS export.

The PS2 menu does not ship any Sony art: it loads PS2.PAK from the root of the openMenu disc
image (next to OPENMENU.INI) at start-up. Without the file the BIOS-style menu is used instead.

  ps2pack.py ps2bios_export.zip PS2.PAK
  ps2pack.py path/to/ps2bios_export PS2.PAK

The input is the export folder (or the zip of it) with assets/textures, assets/icons and
assets/fonts. Needs Pillow and numpy (pip install pillow numpy). Do not commit the result.

PS2.PAK layout (little endian, every blob 32-byte aligned, see ps2_pack.h):
  header   "PS2P", version u32, entry count u32, directory offset u32
  entry    name[16], kind u32, offset u32, size u32, p0..p4 u32   (48 bytes)
  kind 1   texture: p0 w, p1 h (powers of two), p2 format (0 ARGB4444, 1 RGB565, 2 ARGB1555),
           p3/p4 size of the picture inside the padded texture; pixels row by row, not twiddled
  kind 2   model: p0 vertex count; a triangle list of 8 floats per vertex (x y z nx ny nz u v)
  kind 3   font metrics: p0 first character, p1 glyph count, p2 cell width, p3 cell height;
           per glyph u8 left edge, u8 width of the drawn part, u8 advance, u8 unused
"""
import io
import os
import struct
import sys
import tempfile
import zipfile

import numpy as np
from PIL import Image

MAGIC = b"PS2P"
VERSION = 1
KIND_TEX, KIND_MODEL, KIND_FONT = 1, 2, 3
FMT_4444, FMT_565, FMT_1555 = 0, 1, 2

# How each texture is turned into pixels:
#   glow  white, alpha = brightness of the picture (drawn additively, tinted by the vertex colour)
#   rgba  picture with its own alpha
#   rgb   opaque colour picture
GLOW = ["OFOG0", "OFOG1", "OFOG2", "OFOG3", "OFOG4", "OFLAR", "OREF", "CBLUR", "CFLOW", "CSMOK", "CREFA", "CNAVI", "OBLP"]
RGBA = ["BARRW", "BBTTN", "BCDPB", "BCPAR", "BNAV1", "BNAV2", "BOVAL", "CMARU", "OCRBL", "OCRLE", "BICHI"]
RGB = ["CKABE", "OWAL0"]
# The icon pictures are dark (the PS2 lights them with shiny materials); the console's texture
# modulation can only darken, so the dark ones are brightened here.
GAIN = {"BPS2M": 1.7, "BPS1M": 1.7, "BFNOR": 1.6, "BFSCE": 1.6, "BYSYS": 2.0, "BPS1D": 1.6, "BCDDA": 1.3, "BPKST": 1.0}
ICONS = ["BCDDA", "BDISC", "BDVDD", "BFNOR", "BFSCE", "BPKST", "BPS1D", "BPS1M", "BPS2D", "BPS2M", "BYSYS"]

FONT_SHEET = "FNTASCII_256x480_4bpp.png"
FONT_COLS, FONT_ROWS, CELL_W, CELL_H = 8, 12, 32, 40
FONT_FIRST = 0x20


def pow2(n):
    p = 1
    while p < n:
        p <<= 1
    return p


def pad(img, w, h, fill):
    out = Image.new(img.mode, (w, h), fill)
    out.paste(img, (0, 0))
    return out


def to_4444(rgba):
    a = np.asarray(rgba.convert("RGBA"), dtype=np.uint16)
    v = ((a[..., 3] >> 4) << 12) | ((a[..., 0] >> 4) << 8) | ((a[..., 1] >> 4) << 4) | (a[..., 2] >> 4)
    return v.astype("<u2").tobytes()


def to_565(rgb):
    a = np.asarray(rgb.convert("RGB"), dtype=np.uint16)
    v = ((a[..., 0] >> 3) << 11) | ((a[..., 1] >> 2) << 5) | (a[..., 2] >> 3)
    return v.astype("<u2").tobytes()


class Pack:
    def __init__(self):
        self.entries = []  # (name, kind, blob, p0..p4)

    def add(self, name, kind, blob, p=(0, 0, 0, 0, 0)):
        assert len(name) < 16
        self.entries.append((name, kind, blob, tuple(p)))

    def add_texture(self, name, img, mode):
        w, h = img.size
        pw, ph = pow2(max(w, 8)), pow2(max(h, 8))
        if mode == "glow":
            lum = np.asarray(img.convert("L"), dtype=np.uint8)
            rgba = np.dstack([np.full_like(lum, 255)] * 3 + [lum])
            img = Image.fromarray(rgba, "RGBA")
            img = pad(img, pw, ph, (255, 255, 255, 0))
            self.add(name, KIND_TEX, to_4444(img), (pw, ph, FMT_4444, w, h))
        elif mode == "rgba":
            img = pad(img.convert("RGBA"), pw, ph, (0, 0, 0, 0))
            self.add(name, KIND_TEX, to_4444(img), (pw, ph, FMT_4444, w, h))
        else:
            img = pad(img.convert("RGB"), pw, ph, (0, 0, 0))
            self.add(name, KIND_TEX, to_565(img), (pw, ph, FMT_565, w, h))

    def write(self, path):
        count = len(self.entries)
        offset = (16 + 48 * count + 31) & ~31
        blob_at = []
        for _, _, blob, _ in self.entries:
            blob_at.append(offset)
            offset = (offset + len(blob) + 31) & ~31
        out = bytearray(MAGIC + struct.pack("<III", VERSION, count, 16))
        for (name, kind, blob, p), at in zip(self.entries, blob_at):
            out += name.encode("ascii").ljust(16, b"\0") + struct.pack("<IIIIIIII", kind, at, len(blob), *p)
        out += b"\0" * (blob_at[0] - len(out)) if blob_at else b""
        for (_, _, blob, _), at in zip(self.entries, blob_at):
            out += b"\0" * (at - len(out)) + blob
        out += b"\0" * (offset - len(out))
        with open(path, "wb") as f:
            f.write(out)
        print("wrote %s: %d entries, %d bytes" % (path, count, len(out)))


def read_obj(path):
    pos, uv, nrm = [], [], []
    faces = []
    with open(path) as f:
        for line in f:
            t = line.split()
            if not t:
                continue
            if t[0] == "v":
                pos.append([float(x) for x in t[1:4]])
            elif t[0] == "vt":
                uv.append([float(x) for x in t[1:3]])
            elif t[0] == "vn":
                nrm.append([float(x) for x in t[1:4]])
            elif t[0] == "f":
                faces.append([[int(i) - 1 for i in c.split("/")] for c in t[1:4]])
    data = bytearray()
    for tri in faces:
        for v, t, n in tri:
            data += struct.pack("<8f", *pos[v], *nrm[n], uv[t][0], 1.0 - uv[t][1])
    return bytes(data), len(faces) * 3


def font_pack(sheet, pack):
    a = np.asarray(Image.open(sheet).convert("L"), dtype=np.float32)
    assert a.shape == (FONT_ROWS * CELL_H, FONT_COLS * CELL_W), a.shape
    # The sheet has a white face with a grey halo (value 68 of 255): turn it into a white glyph
    # with a dark outline, which is how the PS2 text looks on any background.
    face = np.clip((a - 100.0) / (255.0 - 100.0), 0.0, 1.0)
    halo = (a > 0).astype(np.float32) * 0.7
    alpha = np.maximum(face, halo)
    rgb = face * 255.0
    rgba = np.dstack([rgb, rgb, rgb, alpha * 255.0]).astype(np.uint8)
    img = pad(Image.fromarray(rgba, "RGBA"), 256, 512, (0, 0, 0, 0))
    pack.add("FONT", KIND_TEX, to_4444(img), (256, 512, FMT_4444, 256, FONT_ROWS * CELL_H))
    metrics = bytearray()
    for c in range(FONT_COLS * FONT_ROWS):
        r, col = divmod(c, FONT_COLS)
        cell = a[r * CELL_H:(r + 1) * CELL_H, col * CELL_W:(col + 1) * CELL_W]
        xs = np.nonzero((cell > 0).any(0))[0]
        if len(xs) == 0:  # space and unused cells
            metrics += bytes([0, 0, 9 if c == 0 else 0, 0])
            continue
        left, right = int(xs.min()), int(xs.max())
        width = right - left + 1
        # the halo of a glyph is about 3 pixels wide: letters may overlap it a little
        metrics += bytes([left, width, max(4, width - 3), 0])
    pack.add("FONTMETRICS", KIND_FONT, bytes(metrics), (FONT_FIRST, FONT_COLS * FONT_ROWS, CELL_W, CELL_H, 0))


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    src, dst = sys.argv[1], sys.argv[2]
    tmp = None
    if zipfile.is_zipfile(src):
        tmp = tempfile.TemporaryDirectory()
        with zipfile.ZipFile(src) as z:
            z.extractall(tmp.name)
        src = tmp.name
    root = None
    for dirpath, dirnames, _ in os.walk(src):
        if "assets" in dirnames:
            root = os.path.join(dirpath, "assets")
            break
    if not root:
        print("no assets folder in", src)
        return 1

    pack = Pack()
    for name in GLOW:
        pack.add_texture(name, Image.open("%s/textures/TEX%s.png" % (root, name)), "glow")
    for name in RGBA:
        pack.add_texture(name, Image.open("%s/textures/TEX%s.png" % (root, name)), "rgba")
    for name in RGB:
        pack.add_texture(name, Image.open("%s/textures/TEX%s.png" % (root, name)), "rgb")
    for name in ICONS:
        png = "%s/icons/ICO%s.png" % (root, name)
        obj = "%s/icons/ICO%s.obj" % (root, name)
        if os.path.exists(png):
            img = Image.open(png).convert("RGB")
            gain = GAIN.get(name, 1.0)
            if gain != 1.0:
                img = Image.fromarray(np.clip(np.asarray(img, dtype=np.float32) * gain, 0, 255).astype(np.uint8), "RGB")
            pack.add_texture("T" + name, img, "rgb")
        if os.path.exists(obj):
            blob, n = read_obj(obj)
            pack.add("M" + name, KIND_MODEL, blob, (n, 0, 0, 0, 0))
    font_pack("%s/fonts/%s" % (root, FONT_SHEET), pack)
    pack.write(dst)
    return 0


if __name__ == "__main__":
    sys.exit(main())
