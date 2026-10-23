"""Draws "Now!" and "DCNET" for the header logo by hand, in the style of the Dreamcast typeface (flared humanist capitals: thin
horizontals and bowls tops, thicker stems, wedge-shaped arm and foot ends, a thick diagonal in the N, round O-shapes with thin
tops) at the size of the BIOS wordmark in the 128x32 logo texture (capitals from y 7.6 to 24.6). Every outline here is drawn
from scratch; the typeface was only looked at as a guide. Writes src/openmenu/src/logo_text.inc: 8-bit coverage masks
(255 = ink), 32 rows each. Run: python3 tools/gen_logo_text.py  (needs Pillow). GEN_PREVIEW_DIR gets a preview picture."""
import math, os
from PIL import Image, ImageDraw

SS = 16
TOP, BOT = 7.6, 24.6
CAP = BOT - TOP
XT = 12.4                  # x-height top
MID = (TOP + BOT) / 2
TS = 2.1                   # thick stroke (stems, thick diagonals)
TN = 1.25                  # thin stroke (arms, tops and bottoms of bowls)
SX = 0.9                   # glyphs are drawn a little wide and squeezed horizontally by this
FL = 0.6                   # flare added at stroke ends

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

def D(ox):
    g = Glyph(ox)
    stem(g, 1.5, TOP, BOT, TS, 0.5, 0.5)
    bx, rxo = 6.2, 9.6
    g.poly([(1.5, TOP), (bx, TOP), (bx, TOP + TN), (1.5, TOP + TN)])
    g.poly([(1.5, BOT - TN), (bx, BOT - TN), (bx, BOT), (1.5, BOT)])
    ring(g, bx, MID, rxo - 0.4, CAP / 2, TS, TN, 90, -90)
    return g, 16.4
def C(ox):
    g = Glyph(ox)
    cx = 8.6
    ring(g, cx, MID, 8.2, CAP / 2, TS, TN, 38, 322)
    return g, 16.3
def N(ox):
    g = Glyph(ox)
    stem(g, 1.0, TOP, BOT, 1.3, 0.5, 0.5)
    stem(g, 15.4, TOP, BOT, 1.3, 0.5, 0.5)
    g.poly([(0.4, TOP), (3.6, TOP), (15.9, BOT), (12.7, BOT)])   # the thick diagonal
    return g, 16.2
def E(ox):
    g = Glyph(ox)
    stem(g, 1.5, TOP, BOT, TS, 0.5, 0.5)
    arm(g, 1.5, 11.4, TOP + TN / 2, up=False)
    arm(g, 1.5, 9.6, MID, up=True)
    arm(g, 1.5, 11.6, BOT - TN / 2, up=True)
    return g, 12.2
def T(ox):
    g = Glyph(ox)
    g.poly([(0.2, TOP), (13.6, TOP), (13.6, TOP + TN), (0.2, TOP + TN)])
    g.poly([(0.2, TOP + TN), (0.2, TOP + TN + FL + 0.7), (1.6, TOP + TN)])
    g.poly([(13.6, TOP + TN), (13.6, TOP + TN + FL + 0.7), (12.2, TOP + TN)])
    stem(g, 6.9, TOP, BOT, TS, 0, 0.8)
    return g, 14.0
def o(ox):
    g = Glyph(ox)
    ring(g, 6.4, (XT + BOT) / 2, 6.4, (BOT - XT) / 2, TS - 0.2, TN)
    return g, 12.8
def w(ox):
    g = Glyph(ox)
    # thick down strokes, thin up strokes
    g.poly([(0.0, XT), (2.5, XT), (6.1, BOT), (4.1, BOT)])
    g.poly([(4.1, BOT), (6.1, BOT), (9.3, XT + 1.0), (8.0, XT + 1.0)])
    g.poly([(7.4, XT + 1.0), (9.9, XT + 1.0), (13.5, BOT), (11.5, BOT)])
    g.poly([(11.5, BOT), (13.5, BOT), (16.9, XT), (15.5, XT)])
    return g, 17.0
def bang(ox):
    g = Glyph(ox)
    g.poly([(0.0, TOP), (3.3, TOP), (2.55, 19.6), (0.75, 19.6)])
    g.ellipse(1.65, BOT - 1.35, 1.45, 1.4)
    return g, 3.3

def render(glyphs, width, gaps, x0=0.8):
    from PIL import ImageChops
    out = Image.new('L', (200 * SS, 32 * SS), 0)
    x = x0
    for fn, gap in zip(glyphs, gaps):
        g, adv = fn(x)
        out = ImageChops.lighter(out, g.img)
        x += adv * SX + gap
    return out.crop((0, 0, width * SS, 32 * SS)).resize((width, 32), Image.BOX)

if __name__ == '__main__':
    masks = {'logo_dcnet_mask': (render([D, C, N, E, T], 88, [1.6, 1.7, 1.5, 1.5, 0]), 88),
             'logo_now_mask': (render([N, o, w, bang], 56, [1.5, 1.1, 1.2, 0]), 56)}
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, '..', 'src', 'openmenu', 'src', 'logo_text.inc'), 'w') as f:
        f.write('/* generated by tools/gen_logo_text.py (hand-drawn outlines): coverage masks (255 = ink, 32 rows) of "DCNET" and "Now!" */\n')
        for name, (im, wd) in masks.items():
            f.write('#define %s_W %d\nstatic const uint8_t %s[%d * 32] = {' % (name.upper(), wd, name, wd))
            for i, v in enumerate(im.tobytes()):
                if i % 24 == 0:
                    f.write('\n    ')
                f.write('%d,' % v)
            f.write('\n};\n')
    out = os.environ.get('GEN_PREVIEW_DIR')
    if out:
        sheet = Image.new('L', (88 + 4 + 56, 32), 255)
        for im, off in ((masks['logo_dcnet_mask'][0], 0), (masks['logo_now_mask'][0], 92)):
            sheet.paste(Image.eval(im, lambda v: 255 - v), (off, 0))
        sheet.resize((sheet.width * 8, sheet.height * 8), Image.NEAREST).save(os.path.join(out, 'logo_text_preview.png'))
    print('ok')
