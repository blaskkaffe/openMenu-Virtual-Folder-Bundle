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

# Large flat areas use colours that the Dreamcast shows without a dither pattern. Its 16-bit picture is RGB565 and the PVR dithers
# whatever it cannot show exactly: a texture value v is widened to 8 bits as (v<<3)|(v>>2) (red, blue; green (v<<2)|(v>>4)), which
# is exactly v<<3 only while v is below 4 (below 16 for green). So red and blue of a big fill are one of 0, 8, 16, 24 and its green a
# multiple of 4 up to 60. The page's #111 / #1b1b1b become (16,16,16) / (24,24,24).
PAGE = (16, 16, 16)                     # the web page's background (#111)
CARD = (24, 24, 24)                     # its card colour (#1b1b1b)
TEXT = (238, 238, 238)                  # its text colour (#eee)

# the web page's palette (page kit): normal colour and the lighter one used for borders
THEMES = [
    {"folder": "FOLDERS_8", "name": "WebOrange", "base": (232, 118, 28), "light": (246, 178, 122), "tint": (24, 16, 8), "tint4": (34, 17, 0), "fill": (20, 20, 20)},   # DCNow!
    {"folder": "FOLDERS_9", "name": "WebBlue", "base": (28, 111, 232), "light": (128, 177, 246), "tint": (8, 16, 24), "tint4": (0, 17, 34), "fill": (20, 20, 20)},    # DCNET
]

# where things sit (640x480). The list text and the artwork are drawn by openMenu on top of this picture at the THEME.INI positions.
PANELS = [(10, 66, 398, 382), (414, 66, 214, 138), (414, 210, 214, 214), (414, 428, 214, 20)]   # x, y, w, h of the glass panels
PANEL_ALPHA = 200      # the web page's boxes are rgba(20,20,20,.78) over the scene
LIST_BOX = (10, 66, 408, 448)
LEGEND_BOX = (414, 66, 628, 204)
ART_BOX = (414, 210, 628, 424)
DETAILS_BOX = (414, 428, 628, 448)
RADIUS = 12
BORDER = 3


def mix(a, b, t):
    return tuple(int(round(a[i] * (1 - t) + b[i] * t)) for i in range(3))


def rounded(canvas, box, fill, border, radius=RADIUS, width=BORDER, alpha=255):
    """Anti-aliased rounded box with a border, drawn at SS times the size and shrunk. alpha is the fill's opacity (the border is solid)."""
    x0, y0, x1, y1 = box
    w, h = x1 - x0, y1 - y0
    big = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    d.rounded_rectangle((0, 0, w * SS - 1, h * SS - 1), radius=radius * SS, fill=border + (255,))
    d.rounded_rectangle((width * SS, width * SS, w * SS - 1 - width * SS, h * SS - 1 - width * SS),
                        radius=max(1, (radius - width)) * SS, fill=fill + (alpha,))
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


def cut(full, box, accent=None, swirl_width=0):
    """A piece of the default picture as RGBA: black becomes see-through. With accent, the swirl (the first swirl_width columns) is
    redrawn in that colour, in proportion to how bright each pixel is, so its soft edge does not keep a rim of the old orange."""
    piece = full.crop(box).convert("RGBA")
    px = piece.load()
    for y in range(piece.height):
        for x in range(piece.width):
            r, g, b, _ = px[x, y]
            top = max(r, g, b)
            if top < 24:
                px[x, y] = (0, 0, 0, 0)
            elif accent is not None and x < swirl_width:
                px[x, y] = tuple(int(round(accent[i] * top / 255.0)) for i in range(3)) + (255,)
    return piece


def cut_alpha(full, box, accent=None, swirl_width=0):
    """Like cut(), but a pixel's brightness becomes its opacity and its colour the full colour: white or the accent over whatever
    is behind, with a soft edge that suits the moving backdrop instead of a dark rim."""
    piece = full.crop(box).convert("RGBA")
    px = piece.load()
    for y in range(piece.height):
        for x in range(piece.width):
            r, g, b, _ = px[x, y]
            top = max(r, g, b)
            if top < 24:
                px[x, y] = (0, 0, 0, 0)
            elif accent is not None and x < swirl_width:
                px[x, y] = accent + (top,)
            else:
                px[x, y] = (r * 255 // top, g * 255 // top, b * 255 // top, top)
    return piece


FONT = "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"
FONT_BOLD = "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"
# the button icons are the default Folders theme's own: 16x16 pieces of its picture at x=451 (A, B, X, Y and the Start triangle)
BUTTONS = [(88, "Launch Disc"), (110, "Previous Folder"), (132, "Extras"), (154, "Exit to Bios"), (176, "Settings Menu")]


def legend(img, theme, animated=False, full=None):
    """The controls box the way the web page draws an info box: a small label on top, then rows divided by thin lines."""
    from PIL import ImageFont
    x0, y0, x1, y1 = LEGEND_BOX
    d = ImageDraw.Draw(img)
    small, bold, tiny = ImageFont.truetype(FONT, 13), ImageFont.truetype(FONT_BOLD, 13), ImageFont.truetype(FONT_BOLD, 9)
    label = "Controls"
    d.text(((x0 + x1) // 2, y0 + 12), label, font=bold, fill=theme["light"], anchor="mm")
    line = mix(CARD, theme["light"], 0.22)
    row_h, top = 20, y0 + 22
    for i, (src_y, text) in enumerate(BUTTONS):
        y = top + i * row_h
        d.line((x0 + 10, y, x1 - 11, y), fill=(theme["light"] + (70,)) if animated else line)
        cy, cx = y + row_h // 2 + 1, x0 + 22
        icon = cut_alpha(full, (451, src_y, 467, src_y + 16))      # brightness is opacity, so it sits on the box's fill
        img.alpha_composite(icon, (cx - 8, cy - 8))
        d.text((x0 + 40, cy), text, font=small, fill=TEXT, anchor="lm")


def picture(theme, full, animated=False):
    """The 640x512 picture. Static: an opaque RGB page. Animated: RGBA, the page and the gaps are see-through (the backdrop is drawn
    behind it by openMenu) and the boxes are 80 % opaque, the way the web page's boxes are over its animated background."""
    base, light = theme["base"], theme["light"]
    if animated:
        img = Image.new("RGBA", (640, 512), (0, 0, 0, 0))
        tint, a = theme["tint4"], 204
    else:
        # a still picture of the wave scene (one frame of the animated backdrop) behind boxes that are baked 78 % opaque
        img = Image.new("RGBA", (640, 512), PAGE + (255,))
        scene = os.path.join(HERE, "backdrop_frame.png")
        img.paste(Image.open(scene).convert("RGB") if os.path.exists(scene) else backdrop_frame(theme, 2.0), (0, 0))
        tint, a = theme["fill"], PANEL_ALPHA
    if not animated:
        rounded(img, LIST_BOX, tint, light, alpha=a)
        rounded(img, LEGEND_BOX, tint, light, alpha=a)
        rounded(img, ART_BOX, tint, light, alpha=a)
        rounded(img, DETAILS_BOX, tint, light, radius=10, width=2, alpha=a)
    # the logo, from the default picture, with the swirl in the theme's colour
    logo = (cut_alpha if animated else cut)(full, (12, 16, 190, 58), accent=base, swirl_width=42)
    if animated:      # over the bright sky the white lettering needs a soft shadow, as the web page's heading has
        alpha = logo.getchannel("A").point(lambda v: v * 3 // 4).filter(ImageFilter.GaussianBlur(1.6))
        shadow = Image.new("RGBA", logo.size, (0, 0, 0, 0))
        shadow.putalpha(alpha)
        img.alpha_composite(shadow, (13, 18))
    img.alpha_composite(logo, (12, 16))
    legend(img, theme, animated, full)
    # an empty disc where there is no cover art (openMenu draws the art over it)
    cx, cy = (ART_BOX[0] + ART_BOX[2]) // 2, (ART_BOX[1] + ART_BOX[3]) // 2
    dim = mix(tint, light, 0.40)
    ring(img, (cx, cy), 92, 3, dim)
    ring(img, (cx, cy), 30, 6, dim)
    ring(img, (cx, cy), 14, 3, dim)
    return img if animated else img.convert("RGB")


def backdrop_frame(theme, t, size=(640, 480)):
    """A plain sky gradient, used for the previews when backdrop_frame.png (from render_backdrop_frame.py) is missing."""
    import numpy as np
    w, h = size
    top, bottom = np.array([142, 179, 209.0]), np.array([90, 115, 167.0])
    f = np.linspace(0, 1, h)[:, None, None]
    return Image.fromarray(np.clip(top + (bottom - top) * f + np.zeros((h, w, 3)), 0, 255).astype("uint8"))


def panels_preview(img, theme, alpha, radius=12, border=3):
    """The glass panels as openMenu draws them (shadow, a fill a little lighter at the top and more transparent at the bottom, a border),
    over a preview frame."""
    import numpy as np
    for (x, y, w, h) in PANELS:
        for kind in ("shadow", "fill", "ring"):
            ox, oy = (3, 5) if kind == "shadow" else (0, 0)
            big = Image.new("L", (w * SS, h * SS), 0)
            ImageDraw.Draw(big).rounded_rectangle((0, 0, w * SS - 1, h * SS - 1), radius=radius * SS, fill=255)
            if kind == "ring":
                inner = Image.new("L", (w * SS, h * SS), 0)
                ImageDraw.Draw(inner).rounded_rectangle((border * SS, border * SS, w * SS - 1 - border * SS, h * SS - 1 - border * SS),
                                                      radius=(radius - border) * SS, fill=255)
                big = Image.fromarray(np.clip(np.array(big, dtype=int) - np.array(inner, dtype=int), 0, 255).astype("uint8"))
            mask = np.array(big.resize((w, h), Image.LANCZOS), dtype=float) / 255.0
            rgb = np.zeros((h, w, 3)); a = np.zeros((h, w))
            grad = np.linspace(0, 1, h)[:, None] * np.ones((1, w))
            if kind == "shadow":
                a = (0x50 + (0x30 - 0x50) * grad) / 255.0
            elif kind == "fill":
                top = [min(255, c * 3 // 2) for c in theme["fill"]]
                for k in range(3):
                    rgb[:, :, k] = top[k] + (theme["fill"][k] - top[k]) * grad
                a = (alpha + (alpha * 17 // 20 - alpha) * grad) / 255.0
            else:
                for k in range(3):
                    rgb[:, :, k] = theme["light"][k]
                a = np.ones((h, w))
            region = np.array(img.crop((x + ox, y + oy, x + ox + w, y + oy + h)), dtype=float)
            mix_a = (a * mask)[:, :, None]
            col = rgb if kind != "shadow" else np.zeros((h, w, 3))
            region = region * (1 - mix_a) + col * mix_a
            img.paste(Image.fromarray(np.clip(region, 0, 255).astype("uint8")), (x + ox, y + oy))
    return img


def snap(c):
    """A colour openMenu draws exactly: the Dreamcast keeps 5 bits of red and blue and 6 of green, and dithers the rest."""
    return (c[0] & ~7, c[1] & ~3, c[2] & ~7)


def ini(theme, animated=False):
    base, light = theme["base"], theme["light"]
    sel = mix(CARD, base, 0.38)                        # the cursor bar: the box colour, dimmed
    rgb = lambda c: "%d,%d,%d" % snap(c)
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
menu_corner_radius=10
online_color=72,216,96
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
%s""" % (theme["name"] + ("Anim" if animated else ""), rgb(TEXT), rgb(light), rgb(sel), rgb(light), rgb(PAGE), rgb(TEXT), rgb(light), rgb(CARD), rgb(light),
       rgb(TEXT), rgb(light), ("backdrop=1\n%s" % panel_keys(theme)) if animated else "")


def panel_keys(theme):
    """THEME.INI lines for the glass panels openMenu draws under the picture."""
    rgb = lambda c: "%d,%d,%d" % c
    lines = ["panel_%d=%d,%d,%d,%d" % ((i,) + p) for i, p in enumerate(PANELS)]
    return "\n".join(lines) + "\npanel_border_color_dcnow=246,178,122\npanel_border_color_dcnet=128,177,246\npanel_border_color=%s\npanel_fill_color=%s\npanel_alpha=%d\npanel_radius=%d\npanel_border_width=%d\n" % (
        rgb(theme["light"]), rgb(theme["fill"]), PANEL_ALPHA, RADIUS, BORDER)


def write(theme, out_dir, animated=False):
    import numpy as np
    folder = os.path.join(out_dir, theme["folder"])
    os.makedirs(folder, exist_ok=True)
    img = picture(theme, default_picture(), animated)
    left = img.crop((0, 0, 512, 512))
    right = img.crop((512, 0, 640, 512))
    left.save(os.path.join(folder, "BG_L.PNG"))
    right.save(os.path.join(folder, "BG_R.PNG"))
    with open(os.path.join(folder, "BG_L.PVR"), "wb") as f:
        f.write(pvr.encode(np.array(left), 1025, twiddled=True, argb4444=animated))
    with open(os.path.join(folder, "BG_R.PVR"), "wb") as f:
        f.write(pvr.encode(np.array(right), 1026, twiddled=False, argb4444=animated))
    with open(os.path.join(folder, "THEME.INI"), "w") as f:
        f.write(ini(theme, animated))
    name = theme["name"] + ("Anim" if animated else "")
    if animated:      # what it looks like over one frame of the backdrop
        scene = os.path.join(HERE, "backdrop_frame.png")        # from render_backdrop_frame.py
        base_frame = Image.open(scene).convert("RGB") if os.path.exists(scene) else backdrop_frame(theme, 2.0).convert("RGB")
        frame = panels_preview(base_frame, theme, PANEL_ALPHA).convert("RGBA")
        frame.alpha_composite(img.crop((0, 0, 640, 480)))
        frame.convert("RGB").save(os.path.join(out_dir, name + "_preview.png"))
    else:
        img.crop((0, 0, 640, 480)).save(os.path.join(out_dir, name + "_preview.png"))
    return folder


if __name__ == "__main__":
    # out/ holds the still themes, out_animated/ the same slots with the animated backdrop: install one set or the other.
    base_out = sys.argv[1] if len(sys.argv) > 1 else HERE
    for t in THEMES:
        print("wrote", write(t, os.path.join(base_out, "out")))
        print("wrote", write(t, os.path.join(base_out, "out_animated"), animated=True))
