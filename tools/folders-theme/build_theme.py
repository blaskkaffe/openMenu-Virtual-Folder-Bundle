#!/usr/bin/env python3
"""Builds the openMenu Folders themes that follow the DreamPi web page's look: a dark page, rounded boxes with a coloured border.
One theme per network colour of the web page: orange for DCNow! and blue for DCNET. Writes, per theme, a folder with THEME.INI,
BG_L.PNG / BG_R.PNG (the 640x480 picture split into 512x512 and 128x512) and BG_L.PVR / BG_R.PVR (what the Dreamcast reads;
pvr.py was checked to reproduce the default theme's PVR files byte for byte).

Needs Pillow and numpy:  python3 build_theme.py [out_dir]
The logo and the button legend are cut out of the default Folders theme's background (so that file must be reachable: DEFAULT below)."""
import os
import sys
from PIL import Image, ImageDraw, ImageFilter

import pvr

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT = os.path.join(HERE, "..", "..", "GD MENU Card Manager", "src", "GDMENUCardManager.Core", "tools", "openMenu",
                       "menu_data", "theme", "FOLDERS")
SS = 4                                  # supersampling for the rounded corners

PAGE = (17, 17, 17)                     # the web page's background (#111)
CARD = (27, 27, 27)                     # its card colour (#1b1b1b)
TEXT = (238, 238, 238)                  # its text colour (#eee)

# the web page's palette (page kit): normal colour and the lighter one used for borders
THEMES = [
    {"folder": "FOLDERS_8", "name": "WebOrange", "base": (232, 118, 28), "light": (246, 178, 122)},   # DCNow!
    {"folder": "FOLDERS_9", "name": "WebBlue", "base": (28, 111, 232), "light": (128, 177, 246)},     # DCNET
]

# where things sit (640x480). The list text and the artwork are drawn by openMenu on top of this picture at the THEME.INI positions.
LIST_BOX = (10, 66, 408, 448)
LEGEND_BOX = (414, 66, 628, 204)
ART_BOX = (414, 210, 628, 424)
DETAILS_BOX = (414, 428, 628, 448)
RADIUS = 12
BORDER = 3


def mix(a, b, t):
    return tuple(int(round(a[i] * (1 - t) + b[i] * t)) for i in range(3))


def rounded(canvas, box, fill, border, radius=RADIUS, width=BORDER):
    """Anti-aliased rounded box with a border, drawn at SS times the size and shrunk."""
    x0, y0, x1, y1 = box
    w, h = x1 - x0, y1 - y0
    big = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    d.rounded_rectangle((0, 0, w * SS - 1, h * SS - 1), radius=radius * SS, fill=border + (255,))
    d.rounded_rectangle((width * SS, width * SS, w * SS - 1 - width * SS, h * SS - 1 - width * SS),
                        radius=max(1, (radius - width)) * SS, fill=fill + (255,))
    small = big.resize((w, h), Image.LANCZOS)
    canvas.alpha_composite(small, (x0, y0))


def ring(canvas, centre, radius, width, colour):
    cx, cy = centre
    size = (radius + 2) * 2
    big = Image.new("RGBA", (size * SS, size * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    c = size * SS // 2
    d.ellipse((c - radius * SS, c - radius * SS, c + radius * SS, c + radius * SS), outline=colour + (255,), width=width * SS)
    canvas.alpha_composite(big.resize((size, size), Image.LANCZOS), (cx - size // 2, cy - size // 2))


def default_picture():
    l = Image.open(os.path.join(DEFAULT, "BG_L.PNG")).convert("RGB")
    r = Image.open(os.path.join(DEFAULT, "BG_R.PNG")).convert("RGB")
    full = Image.new("RGB", (640, 480))
    full.paste(l.crop((0, 0, 512, 480)), (0, 0))
    full.paste(r.crop((0, 0, 128, 480)), (512, 0))
    return full


def cut(full, box, accent=None):
    """A piece of the default picture as RGBA: black becomes see-through. With accent, the orange swirl takes that colour."""
    piece = full.crop(box).convert("RGBA")
    px = piece.load()
    for y in range(piece.height):
        for x in range(piece.width):
            r, g, b, _ = px[x, y]
            top = max(r, g, b)
            if top < 40:
                px[x, y] = (0, 0, 0, 0)
            elif accent is not None and r > 150 and r > g + 60 and r > b + 60:      # the orange of the swirl
                f = top / 255.0
                px[x, y] = tuple(int(round(accent[i] * f)) for i in range(3)) + (255,)
    return piece


def picture(theme, full):
    base, light = theme["base"], theme["light"]
    img = Image.new("RGBA", (640, 512), PAGE + (255,))
    tint = mix(CARD, base, 0.07)                       # a card with a hint of the box's colour, like the web boxes
    rounded(img, LIST_BOX, tint, light)
    rounded(img, LEGEND_BOX, tint, light)
    rounded(img, ART_BOX, tint, light)
    rounded(img, DETAILS_BOX, mix(CARD, base, 0.30), light, radius=10, width=2)
    # the logo (swirl in the theme's colour) and the button legend, from the default picture
    logo = cut(full, (12, 16, 190, 58), accent=base)
    img.alpha_composite(logo, (12, 16))
    legend = cut(full, (446, 82, 604, 198))
    img.alpha_composite(legend, (446, 82))
    # an empty disc where there is no cover art (openMenu draws the art over it)
    cx, cy = (ART_BOX[0] + ART_BOX[2]) // 2, (ART_BOX[1] + ART_BOX[3]) // 2
    dim = mix(CARD, light, 0.45)
    ring(img, (cx, cy), 92, 3, dim)
    ring(img, (cx, cy), 30, 6, dim)
    ring(img, (cx, cy), 14, 3, dim)
    return img.convert("RGB")


def ini(theme):
    base, light = theme["base"], theme["light"]
    sel = mix(CARD, base, 0.38)                        # the cursor bar: the box colour, dimmed
    rgb = lambda c: "%d,%d,%d" % c
    return """[THEME]
name=%s
text_color=%s
highlight_color=%s
cursor_color=%s
multidisc_color=%s
menu_title_color=%s
menu_text_color=%s
menu_highlight_color=%s
menu_bkg_color=%s
menu_bkg_border_color=%s
list_x=22
list_y=76
list_count=17
list_marquee_threshold=46
artwork_x=420
artwork_y=216
artwork_size=202
item_details_x=521
item_details_y=430
item_details_text_color=%s
clock_x=623
clock_y=36
clock_text_color=%s
""" % (theme["name"], rgb(TEXT), rgb(light), rgb(sel), rgb(light), rgb(PAGE), rgb(TEXT), rgb(light), rgb(CARD), rgb(light),
       rgb(TEXT), rgb(light))


def write(theme, out_dir):
    folder = os.path.join(out_dir, theme["folder"])
    os.makedirs(folder, exist_ok=True)
    img = picture(theme, default_picture())
    left = img.crop((0, 0, 512, 512))
    right = img.crop((512, 0, 640, 512))
    left.save(os.path.join(folder, "BG_L.PNG"))
    right.save(os.path.join(folder, "BG_R.PNG"))
    import numpy as np
    with open(os.path.join(folder, "BG_L.PVR"), "wb") as f:
        f.write(pvr.encode(np.array(left), 1025, twiddled=True))
    with open(os.path.join(folder, "BG_R.PVR"), "wb") as f:
        f.write(pvr.encode(np.array(right), 1026, twiddled=False))
    with open(os.path.join(folder, "THEME.INI"), "w") as f:
        f.write(ini(theme))
    img.crop((0, 0, 640, 480)).save(os.path.join(out_dir, theme["name"] + "_preview.png"))
    return folder


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "out")
    for t in THEMES:
        print("wrote", write(t, out))
