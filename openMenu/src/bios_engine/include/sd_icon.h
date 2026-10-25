/* sd_icon: the little SD card drawn on the memory card slot that the Serial VMU uses (File screen): the back of an SD card as a
 * flat vector shape in three colours (the card, its read / write switch, the gold contacts), sent through a scene sink like the
 * menu's models, so it takes part in their depth order, fades with them and is the same on the console and in the PC preview.
 * Portable C. */
#pragma once

#include "bios_scene.h"

#define SD_ICON_W 24.0f
#define SD_ICON_H 31.0f
/* Where it sits on a card: its lower right corner is this far inside the lower right corner of the card's picture. */
#define SD_ICON_INSET_X 5.0f
#define SD_ICON_INSET_Y 6.0f

/* The icon with its top left corner at (x, y) on the picture, at depth `invw` (1 / distance, as the sink wants it), `alpha` 0..1. */
void sd_icon_draw(const bscene_sink* sink, float x, float y, float invw, float alpha);
