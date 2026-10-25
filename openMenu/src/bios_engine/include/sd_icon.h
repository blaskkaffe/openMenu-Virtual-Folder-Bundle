/* sd_icon: the little SD card drawn on the memory card slot that the Serial VMU uses (File screen). A handful of flat
 * rectangles, so the console draws them with gfx_rect() and the PC preview with its own rasteriser. Portable C. */
#pragma once

#include <stdint.h>

#define SD_ICON_W 21.0f
#define SD_ICON_H 27.0f
#define SD_ICON_MAX_RECTS 24
/* Where it sits on a card: its lower right corner is this far inside the lower right corner of the card's picture. */
#define SD_ICON_INSET_X 6.0f
#define SD_ICON_INSET_Y 8.0f

typedef struct sd_rect {
    float x, y, w, h;
    uint32_t argb;
} sd_rect;

/* The rectangles of the icon with its top left corner at (x, y), in drawing order. Returns how many. */
int sd_icon_rects(float x, float y, sd_rect out[SD_ICON_MAX_RECTS]);
