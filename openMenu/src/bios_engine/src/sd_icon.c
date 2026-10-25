/* sd_icon: see sd_icon.h. The back of an SD card, simple and flat like the menu's models: the corner cut at the top left, a row of
 * contacts along the top and the single one beside the cut, the read / write switch on the right edge. Three colours. */
#include "sd_icon.h"

#define CARD_ARGB 0xFF1F2536u   /* blackish grey-blue */
#define SWITCH_ARGB 0xFF353C52u /* a little lighter */
#define GOLD_ARGB 0xFFF2C81Eu   /* the contacts: one yellow */

#define CUT 6.0f    /* the cut corner (top left) */
#define ROUND 2.5f  /* the other three corners */

static void
tri(const bscene_sink* sink, float invw, uint32_t argb, float x0, float y0, float x1, float y1, float x2, float y2) {
    bscene_vtx v[3] = {{x0, y0, invw, 0.0f, 0.0f, argb, 0}, {x1, y1, invw, 0.0f, 0.0f, argb, 0}, {x2, y2, invw, 0.0f, 0.0f, argb, 0}};
    const bscene_texref none = {BSCENE_TEX_NONE, 0, 0, 0};
    sink->triangle(sink->user, v, none);
}

static void
quad(const bscene_sink* sink, float invw, uint32_t argb, float x, float y, float w, float h) {
    tri(sink, invw, argb, x, y, x + w, y, x, y + h);
    tri(sink, invw, argb, x + w, y, x + w, y + h, x, y + h);
}

static uint32_t
faded(uint32_t argb, float alpha) {
    return (argb & 0x00FFFFFFu) | ((uint32_t)((float)(argb >> 24) * alpha) << 24);
}

void
sd_icon_draw(const bscene_sink* sink, float x, float y, float invw, float alpha) {
    if (!sink || !sink->triangle || alpha <= 0.0f) {
        return;
    }
    const float w = SD_ICON_W, h = SD_ICON_H;
    x = (float)(int)(x + 0.5f); /* on whole pixels, so the stripes of the contacts stay even wherever the card is */
    y = (float)(int)(y + 0.5f);
    const uint32_t card = faded(CARD_ARGB, alpha), sw = faded(SWITCH_ARGB, alpha), gold = faded(GOLD_ARGB, alpha);

    /* the card: a fan from its middle round the outline: the cut corner top left, the other three corners rounded (two points each) */
    const float r = ROUND, d = ROUND * 0.2929f; /* d: how far in a rounded corner's middle point is from the box corner */
    const float px[] = {x + CUT, x + w - r, x + w - d, x + w,     x + w,     x + w - d, x + w - r, x + r,     x + d,     x,         x};
    const float py[] = {y,       y,         y + d,     y + r,     y + h - r, y + h - d, y + h,     y + h,     y + h - d, y + h - r, y + CUT};
    const int n = (int)(sizeof(px) / sizeof(px[0]));
    float cx = x + w / 2.0f, cy = y + h / 2.0f;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        tri(sink, invw, card, cx, cy, px[i], py[i], px[j], py[j]);
    }

    /* the read / write switch on the right edge */
    quad(sink, invw, sw, x + w - 3.0f, y + 12.0f, 3.0f, 6.0f);
    /* the contacts: the one beside the cut, and the row along the top */
    quad(sink, invw, gold, x + 3.0f, y + 7.0f, 2.0f, 6.0f);
    for (int i = 0; i < 5; i++) {
        quad(sink, invw, gold, x + 7.0f + 3.0f * (float)i, y + 2.0f, 2.0f, 8.0f);
    }
}
