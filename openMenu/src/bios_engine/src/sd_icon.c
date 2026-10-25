/* sd_icon: see sd_icon.h. An SD card seen from the front: the corner cut at the top right, the label area, the gold contacts at
 * the bottom; a light edge round it so it shows on any card colour. */
#include "sd_icon.h"

#define CUT 7 /* the cut corner, in pixels */
#define EDGE_ARGB 0xFFF2F2F2u
#define BODY_ARGB 0xFF1C2E66u
#define LABEL_ARGB 0xFFDDE4F5u
#define GOLD_ARGB 0xFFE8B830u

static int
shape(sd_rect* out, int n, float x, float y, float w, float h, int cut, uint32_t argb) {
    /* the staircase of the cut corner: one row at a time, then the straight part */
    for (int r = 0; r < cut; r++) {
        out[n++] = (sd_rect){x, y + (float)r, w - (float)(cut - r), 1.0f, argb};
    }
    out[n++] = (sd_rect){x, y + (float)cut, w, h - (float)cut, argb};
    return n;
}

int
sd_icon_rects(float x, float y, sd_rect out[SD_ICON_MAX_RECTS]) {
    int n = shape(out, 0, x, y, SD_ICON_W, SD_ICON_H, CUT, EDGE_ARGB);
    n = shape(out, n, x + 1.5f, y + 1.5f, SD_ICON_W - 3.0f, SD_ICON_H - 3.0f, CUT - 2, BODY_ARGB);
    out[n++] = (sd_rect){x + 4.0f, y + 9.0f, SD_ICON_W - 8.0f, 8.0f, LABEL_ARGB};
    for (int i = 0; i < 4; i++) {
        out[n++] = (sd_rect){x + 4.0f + (float)i * 3.5f, y + SD_ICON_H - 8.0f, 2.0f, 5.5f, GOLD_ARGB};
    }
    return n;
}
