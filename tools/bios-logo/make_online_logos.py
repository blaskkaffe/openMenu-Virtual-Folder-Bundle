#!/usr/bin/env python3
"""Makes the online header logos of the BIOS-style menu as .PVR files (and PNGs to look at), from a boot ROM dump of your own:

  make_online_logos.py dc_boot.bin outdir

  DCNET_ORANGE.PVR / DCNET_BLUE.PVR  128x32  the swirl of the BIOS logo with "DCNET" in place of the wordmark
  NOW_ORANGE.PVR / NOW_BLUE.PVR       56x32  "Now!" in the swirl's colour, drawn behind the logo (starts 125 px into the logo)

Orange is the logo of NTSC consoles, blue the swirl the menu makes for European (PAL) consoles (red and blue exchanged). The files
are ARGB4444 rectangle textures with a GBIX header, the format of LOGO.PVR (see biologo.py). They contain the ROM's swirl, so they are
for your own use and are not kept in the repository. The letters come from openMenu/tools/gen_logo_text.py (hand-drawn outlines).
Needs Pillow."""
import importlib.util
import os
import struct
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('gen_logo_text', os.path.join(HERE, '..', '..', 'openMenu', 'tools', 'gen_logo_text.py'))
glt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(glt)

W, H = 128, 32
SWIRL_W = 29
DCNET_X = 30


def rom_logo(rom):
    d = open(rom, 'rb').read()
    off = 0x89B50  # the logo with the swirl (the plain one is at 0x7C970)
    if d[off:off + 4] != b'GBIX':
        sys.exit('this does not look like a supported boot ROM')
    p = d.find(b'PVRT', off, off + 0x20)
    fmt, typ = d[p + 8], d[p + 9]
    if typ != 9 or fmt != 2:
        sys.exit('unexpected logo format %d/%d' % (fmt, typ))
    img = Image.new('RGBA', (W, H))
    for i in range(W * H):
        v = struct.unpack_from('<H', d, p + 16 + 2 * i)[0]
        img.putpixel((i % W, i // W), (((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17, ((v >> 12) & 15) * 17))
    return img


def to_blue(img):
    """The PAL swirl: the orange (238, 68, 0) becomes the blue (74, 116, 190), the grey stays (as bios_header_logo in bios_rom)."""
    out = img.copy()
    q = lambda v: ((max(0, min(255, v)) + 8) // 17) * 17
    for y in range(H):
        for x in range(W):
            r, g, b, a = out.getpixel((x, y))
            k = max(0, min(256, (r - b) * 256 // 238))
            r += k * (74 - 238) // 256
            g += k * (116 - 68) // 256
            b += k * (190 - 0) // 256
            out.putpixel((x, y), (q(r), q(g), q(b), a))
    return out


def swirl_colour(img):
    """The most saturated opaque pixel of the swirl: the colour "Now!" takes (as the menu does)."""
    best, col = -1, (59, 187, 221)
    for y in range(H):
        for x in range(SWIRL_W):
            r, g, b, a = img.getpixel((x, y))
            if a < 0xF0:
                continue
            sat = max(r, g, b) - min(r, g, b)
            if sat > best:
                best, col = sat, (r, g, b)
    return col


def write_pvr(img, path):
    img = img.convert('RGBA')
    w, h = img.size
    payload = b''.join(struct.pack('<H', ((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)) for r, g, b, a in img.getdata())
    pvrt = b'PVRT' + struct.pack('<I', len(payload) + 8) + bytes([2, 9, 0, 0]) + struct.pack('<HH', w, h)
    gbix = b'GBIX' + struct.pack('<II', 8, 0) + b'\0' * 4
    with open(path, 'wb') as f:
        f.write(gbix + pvrt + payload)
    img.save(path[:-4] + '.png')
    print('wrote', path, '(%dx%d)' % (w, h))


def main(rom, outdir):
    os.makedirs(outdir, exist_ok=True)
    base = rom_logo(rom)
    dcnet_mask = glt.render([glt.D, glt.C, glt.N, glt.E, glt.T], 88, [1.6, 1.7, 1.5, 1.5, 0])
    now_mask = glt.render([glt.N, glt.o, glt.w, glt.bang], 56, [1.5, 1.1, 1.2, 0])
    for name, logo in (('ORANGE', base), ('BLUE', to_blue(base))):
        dc = Image.new('RGBA', (W, H), (0, 0, 0, 0))
        dc.paste(logo.crop((0, 0, SWIRL_W, H)), (0, 0))
        text = Image.new('RGBA', dcnet_mask.size, (68, 68, 68, 0))
        text.putalpha(dcnet_mask.point(lambda v: (v >> 4) * 17))
        dc.alpha_composite(text, (DCNET_X, 0))
        write_pvr(dc, os.path.join(outdir, 'DCNET_%s.PVR' % name))
        badge = Image.new('RGBA', now_mask.size, swirl_colour(logo) + (0,))
        badge.putalpha(now_mask.point(lambda v: (v >> 4) * 17))
        write_pvr(badge, os.path.join(outdir, 'NOW_%s.PVR' % name))


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
