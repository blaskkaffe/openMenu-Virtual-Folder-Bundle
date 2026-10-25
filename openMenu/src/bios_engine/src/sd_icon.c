/* sd_icon: see sd_icon.h. The back of an SD card as in the Dreamcast BIOS's own pictures: flat colours, a light outline, no gradients.
 * Black body with the corner cut at the top left, a row of gold contacts along the top and the single contact beside the cut,
 * the grey write-protect tab on the right, the groove at the bottom, a few lighter specks for the grain of the plastic. */
#include "sd_icon.h"

#define CUT 3        /* the cut corner (top left), in pixels */
#define STEP_ROWS 11 /* the rows at the top are one pixel narrower on each side, as on a real card */

#define EDGE_ARGB 0xFFE4E6EDu
#define BODY_ARGB 0xFF14161Du
#define GRAIN_ARGB 0xFF2B2E39u
#define GOLD_A_ARGB 0xFFD8B04Au
#define GOLD_B_ARGB 0xFFEED27Cu
#define TAB_ARGB 0xFF7A7F8Bu
#define GROOVE_ARGB 0xFF2B2E39u

/* the silhouette: the cut corner, the narrower upper part, the full lower part */
static int
shape(sd_rect* out, int n, float x, float y, float w, float h, int cut, int step_rows, uint32_t argb) {
    for (int r = 0; r < cut; r++) {
        int left = cut - r > 1 ? cut - r : 1;
        out[n++] = (sd_rect){x + (float)left, y + (float)r, w - (float)left - 1.0f, 1.0f, argb};
    }
    out[n++] = (sd_rect){x + 1.0f, y + (float)cut, w - 2.0f, (float)(step_rows - cut), argb};
    out[n++] = (sd_rect){x, y + (float)step_rows, w, h - (float)step_rows, argb};
    return n;
}

int
sd_icon_rects(float x, float y, sd_rect out[SD_ICON_MAX_RECTS]) {
    static const unsigned char grain[][2] = {{5, 14}, {9, 17}, {14, 13}, {18, 19}, {7, 21}, {12, 24}, {17, 15}, {20, 23}, {4, 25}};
    int n = shape(out, 0, x, y, SD_ICON_W, SD_ICON_H, CUT, STEP_ROWS, EDGE_ARGB);
    n = shape(out, n, x + 1.0f, y + 1.0f, SD_ICON_W - 2.0f, SD_ICON_H - 2.0f, CUT - 1, STEP_ROWS - 1, BODY_ARGB);
    for (unsigned i = 0; i < sizeof(grain) / sizeof(grain[0]); i++) {
        out[n++] = (sd_rect){x + (float)grain[i][0], y + (float)grain[i][1], 1.0f, 1.0f, GRAIN_ARGB};
    }
    out[n++] = (sd_rect){x + 2.0f, y + 5.0f, 2.0f, 5.0f, GOLD_A_ARGB}; /* the contact beside the cut */
    for (int i = 0; i < 5; i++) {
        out[n++] = (sd_rect){x + 6.0f + 3.0f * (float)i, y + 2.0f, 2.0f, 7.0f, i & 1 ? GOLD_B_ARGB : GOLD_A_ARGB};
    }
    out[n++] = (sd_rect){x + SD_ICON_W - 2.0f, y + 10.0f, 2.0f, 4.0f, TAB_ARGB};
    out[n++] = (sd_rect){x + 6.0f, y + SD_ICON_H - 5.0f, 12.0f, 1.0f, GROOVE_ARGB};
    return n;
}
