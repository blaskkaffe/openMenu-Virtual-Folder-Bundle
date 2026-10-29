"""Draws "CNET" and "Now!" for the header logo by hand, to sit with the BIOS "Dreamcast" wordmark in the 128x32 logo texture. The
online DCNET logo is the ROM's own swirl and capital D (cropped at run time, nothing of it is stored) with "CNET" drawn after the D, so
only the letters C N E T are made here, plus "Now!". Measured on the wordmark: capitals from y 7.6 to 24.8, x-height top 12.3, stems 2.9 px,
horizontals 2.45 px, round parts 3 px at the sides and 2.4 px at top and bottom, plain square ends, letters about 1.5 px apart. Every
outline is drawn from scratch (the Dreamcast typeface, personal-use only, was only a visual guide). Each letter is drawn 16 times larger and reduced
by the average of the covered area, which is how the wordmark's edges look (see render()).
Writes logo_letters.inc (the coverage masks of "CNET" and "Now!", 64x32, built into the program by gfx.c) to src/openmenu/src/ (or LOGO_INC).
Run: python3 tools/gen_logo_text.py  (needs Pillow).
GEN_PREVIEW_DIR gets a preview picture."""
import math, os
from PIL import Image, ImageDraw

SS = 16
TOP, BOT = 7.6, 24.8
CAP = BOT - TOP
XT = 12.4                  # x-height top
MID = (TOP + BOT) / 2
TS = 2.7                  # stems
TN = 2.3                  # horizontals (arms, tops and bottoms of bowls)
SX = 1.0
FL = 0.0                   # no flares: the wordmark's ends are plain

class Glyph:
    def __init__(self, ox):
        self.img = Image.new('L', (200 * SS, 32 * SS), 0)
        self.d = ImageDraw.Draw(self.img)
        self.ox = ox
    def poly(self, pts, fill=255):
        self.d.polygon([((x * SX + self.ox) * SS, y * SS) for x, y in pts], fill=fill)
    def ellipse(self, cx, cy, rx, ry, fill=255):
        self.d.ellipse([((cx - rx) * SX + self.ox) * SS, (cy - ry) * SS, ((cx + rx) * SX + self.ox) * SS, (cy + ry) * SS], fill=fill)

def arcpts(cx, cy, rx, ry, a0, a1, n=64):
    return [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * i / n)), cy - ry * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]

def stem(g, xc, y0, y1, w, flare_top=0.0, flare_bot=0.0):
    """A vertical stem of width w whose ends are wider by the flares (a gentle taper, as the typeface's stems)."""
    h = y1 - y0
    g.poly([(xc - w / 2 - flare_top, y0), (xc + w / 2 + flare_top, y0), (xc + w / 2, y0 + 0.22 * h), (xc + w / 2, y1 - 0.22 * h),
            (xc + w / 2 + flare_bot, y1), (xc - w / 2 - flare_bot, y1), (xc - w / 2, y1 - 0.22 * h), (xc - w / 2, y0 + 0.22 * h)])

def arm(g, x0, x1, yc, flare_end=FL, thin=TN, up=True):
    """A horizontal arm: thin at the stem, ending in a wedge that widens towards its end (up or down)."""
    t = thin
    g.poly([(x0, yc - t / 2), (x1 - 1.6, yc - t / 2), (x1, yc - t / 2 - (flare_end if up else 0)), (x1, yc + t / 2 + (0 if up else flare_end)),
            (x1 - 1.6, yc + t / 2), (x0, yc + t / 2)])

def ring(g, cx, cy, rxo, ryo, tx, ty, a0=0, a1=360, n=96):
    outer = arcpts(cx, cy, rxo, ryo, a0, a1, n)
    inner = arcpts(cx, cy, rxo - tx, ryo - ty, a1, a0, n)
    g.poly(outer + inner)

SIDE = 2.85                # the round parts: thickness at the sides
RTB = 2.25                  # and at the top and bottom
def C(ox):
    g = Glyph(ox)
    ring(g, 7.7, MID, 7.7, CAP / 2, SIDE, RTB, 42, 318)
    return g, 15.4
def N(ox):
    g = Glyph(ox)
    stem(g, 1.45, TOP, BOT, TS)
    stem(g, 14.3, TOP, BOT, TS)
    g.poly([(0.0, TOP), (3.5, TOP), (15.75, BOT), (12.25, BOT)])   # the diagonal
    return g, 15.75
def E(ox):
    g = Glyph(ox)
    stem(g, 1.45, TOP, BOT, TS)
    g.poly([(1.45, TOP), (10.6, TOP), (10.6, TOP + TN), (1.45, TOP + TN)])
    g.poly([(1.45, MID - TN / 2), (9.6, MID - TN / 2), (9.6, MID + TN / 2), (1.45, MID + TN / 2)])
    g.poly([(1.45, BOT - TN), (11.0, BOT - TN), (11.0, BOT), (1.45, BOT)])
    return g, 11.0
def T(ox):
    g = Glyph(ox)
    g.poly([(0.0, TOP), (13.8, TOP), (13.8, TOP + TN), (0.0, TOP + TN)])
    stem(g, 6.9, TOP, BOT, TS)
    return g, 13.8
def o(ox):
    g = Glyph(ox)
    ring(g, 6.4, (XT + BOT) / 2, 6.4, (BOT - XT) / 2, SIDE - 0.2, RTB - 0.1)
    return g, 12.8
def w(ox):
    g = Glyph(ox)
    g.poly([(0.0, XT), (3.3, XT), (7.0, BOT), (3.9, BOT)])
    g.poly([(3.9, BOT), (7.0, BOT), (9.9, XT + 1.2), (7.3, XT + 1.2)])
    g.poly([(7.3, XT + 1.2), (9.9, XT + 1.2), (13.8, BOT), (10.7, BOT)])
    g.poly([(10.7, BOT), (13.8, BOT), (17.4, XT), (14.1, XT)])
    return g, 17.4
def bang(ox):
    g = Glyph(ox)
    g.poly([(0.0, TOP), (2.9, TOP), (2.9, 19.2), (0.0, 19.2)])
    g.ellipse(1.45, BOT - 1.5, 1.5, 1.5)
    return g, 2.9

def render(glyphs, width, gaps, x0=0.8):
    from PIL import ImageChops
    out = Image.new('L', (200 * SS, 32 * SS), 0)
    x = x0
    for fn, gap in zip(glyphs, gaps):
        g, adv = fn(x)
        out = ImageChops.lighter(out, g.img)
        x += adv * SX + gap
    # drawn 16 times larger and reduced by the average of the covered area, as the wordmark's own edges were made (its stems measure
    # 0.6, 1, 1, 0.27 across four pixels): the letters keep exactly the weight they were drawn with
    return out.crop((0, 0, width * SS, 32 * SS)).resize((width, 32), Image.BOX)

def write_pvr(img, path):
    """ARGB4444 rectangle texture with a GBIX header: the format of LOGO.PVR (tools/bios-logo/biologo.py). Sizes are powers of two, which
    the PVR needs."""
    import struct
    w, h = img.size
    raw = img.convert('RGBA').tobytes()
    payload = b''.join(struct.pack('<H', ((raw[i + 3] >> 4) << 12) | ((raw[i] >> 4) << 8) | ((raw[i + 1] >> 4) << 4) | (raw[i + 2] >> 4)) for i in range(0, w * h * 4, 4))
    pvrt = b'PVRT' + struct.pack('<I', len(payload) + 8) + bytes([2, 9, 0, 0]) + struct.pack('<HH', w, h)
    gbix = b'GBIX' + struct.pack('<II', 8, 0) + b'\0' * 4
    with open(path, 'wb') as f:
        f.write(gbix + pvrt + payload)


def baked_text(mask, width, colour):
    """The letters in `colour` the way the wordmark's own pixels are made: the coverage c is the alpha, and the colour is the text colour
    mixed with WHITE by (1 - c), c*colour + (1-c)*255, as if the letters were reduced on a white page (an edge pixel of the original D is
    (204, 204, 204) at alpha 68, a half-covered one (153, 170, 170) at alpha 153). Drawn on the grey bar the edges therefore come out a
    little lighter than the bar, which is why the wordmark looks as light as it does. Drawn untinted."""
    img = Image.new('RGBA', (width, 32), (255, 255, 255, 0))
    px = img.load()
    m = mask.load()
    for y in range(32):
        for x in range(mask.size[0]):
            c = m[x, y] / 255.0
            a = int(round(c * 15))
            if a == 0:
                continue
            c = a / 15.0
            px[x, y] = tuple(int(round((c * t + (1 - c) * 255) / 17.0)) * 17 for t in colour) + (a * 17,)
    return img


def flat_text(mask, width, colour):
    """The letters for the dark header bar (dark mode): the colour as it is, the coverage as the alpha, so the edges blend into the bar and
    not into white."""
    img = Image.new('RGBA', (width, 32), colour + (0,))
    px = img.load()
    m = mask.load()
    for y in range(32):
        for x in range(mask.size[0]):
            a = int(round(m[x, y] / 255.0 * 15))
            if a:
                px[x, y] = colour + (a * 17,)
    return img


GREY = (68, 68, 68)       # the wordmark's colour
LIGHT = (205, 205, 208)   # the wordmark's colour on the dark bar
ORANGE = (238, 68, 0)     # the NTSC swirl
BLUE = (68, 119, 187)     # the PAL swirl (what the menu's blue swirl comes to)

if __name__ == '__main__':
    here = os.path.dirname(os.path.abspath(__file__))
    outdir = os.environ.get('LOGO_OUT', os.path.join(here, '..', '..', 'GD MENU Card Manager', 'src', 'GDMENUCardManager.Core', 'tools', 'openMenu', 'menu_data'))
    # CNET starts 1.5 px right of where the wordmark's D ends (the D's bowl reaches column 44.9; the menu draws it from column 46)
    cnet = render([C, N, E, T], 64, [1.5, 1.5, 1.5, 0], x0=1.2)
    now = render([N, o, w, bang], 64, [1.5, 1.2, 1.4, 0], x0=0.8)
    pics = {'CNET.PVR': baked_text(cnet, 64, GREY), 'NOWO.PVR': baked_text(now, 64, ORANGE), 'NOWB.PVR': baked_text(now, 64, BLUE),
            # the same for dark mode (dark grey header bar)
            'CNETD.PVR': flat_text(cnet, 64, LIGHT), 'NOWOD.PVR': flat_text(now, 64, ORANGE), 'NOWBD.PVR': flat_text(now, 64, (96, 150, 225))}
    # The menu has the letters inside its own program: the coverage (0..15) of every pixel of the two words, and builds the six pictures (grey
    # "CNET" and orange / blue "Now!" for the light bar, the dark-bar versions) from them when it starts (gfx.c, build_letter_textures).
    inc = os.environ.get('LOGO_INC', os.path.join(here, '..', 'src', 'openmenu', 'src', 'logo_letters.inc'))
    with open(inc, 'w') as f:
        f.write('/* Generated by tools/gen_logo_text.py: the coverage (0..15) of the pixels of the 64x32 pictures of "CNET" and "Now!", hand-drawn\n'
                ' * outlines, nothing of the BIOS logo or of any font. */\n')
        for name, img in (('logo_mask_cnet', cnet), ('logo_mask_now', now)):
            m = img.load()
            f.write('static const uint8_t %s[64 * 32] = {\n' % name)
            for y in range(32):
                f.write('    ' + ','.join('%d' % int(round(m[x, y] / 255.0 * 15)) for x in range(64)) + ',\n')
            f.write('};\n')
    print('wrote', inc)
    prev = os.environ.get('GEN_PREVIEW_DIR')
    if prev:
        sheet = Image.new('RGB', (68 * len(pics), 32), (170, 170, 170))
        for k, name in enumerate(pics):
            img = pics[name]
            sheet.paste(img.convert('RGB'), (k * 68, 0), img.getchannel('A'))
        sheet.resize((sheet.width * 6, sheet.height * 6), Image.NEAREST).save(os.path.join(prev, 'logo_text_preview.png'))
