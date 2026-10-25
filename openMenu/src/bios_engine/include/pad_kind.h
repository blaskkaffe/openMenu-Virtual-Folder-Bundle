/* pad_kind: what kind of device sits in a controller port, for the label on the Files screen. Pure functions (no KOS),
 * so they are tested on the PC. */
#pragma once

#include <stdint.h>

/* The name to show for the device in a port, from the Maple function bits and the product name of its device info.
 * Returns NULL for a standard Dreamcast controller (nothing is shown for it). Otherwise a short name ("Keyboard", "Mouse",
 * "Light Gun", "Fishing Rod", "Arcade Stick", ...) written to `out` (size >= PAD_KIND_MAX); for a controller-like device
 * that is none of the known ones it is the product name itself, trimmed. */
#define PAD_KIND_MAX 20
const char* pad_kind_name(uint32_t functions, const char* product_name, char out[PAD_KIND_MAX]);

/* Where the label of a port goes on the card grid: a pill over the lower right corner of the controller picture, text centred in
 * it. `pad_x`, `pad_y` is where the grid draws the controller (bfs_pad_px), `text_w` the width of the text in pixels. */
typedef struct pad_label_rect {
    float x, y, w, h;
} pad_label_rect;
#define PAD_LABEL_SCALE 0.55f /* the text, relative to the menu font */
void pad_label_layout(float pad_x, float pad_y, float text_w, pad_label_rect* out);
