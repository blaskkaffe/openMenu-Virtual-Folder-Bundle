/* pad_kind: what kind of device sits in a controller port, for the label on the File screen. Pure functions (no KOS),
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
#define PAD_LABEL_SCALE 1.0f /* the text, relative to the menu font: the BIOS font at its own size */
#define PAD_LABEL_CENTRED 1 /* 1: the pill is centred on the controller; 0: its right edge is the controller's right edge */
#define PILL_BORDER 2.5f    /* the lighter rim of the pill */
#define PAD_LABEL_MAX_W 140.0f /* the widest pill; a column of the grid is about 100 pixels */
/* `centred`: the pill is centred on the controller, else its right edge is the controller's right edge. */
void pad_label_layout(float pad_x, float pad_y, float text_w, int centred, pad_label_rect* out);

/* Where the labels of all ports go: n ports left to right, each with the place of its controller (pad_x, pad_y) and the width of its text
 * (text_w <= 0: no label). A pill sits at the bottom of its controller; one that would touch the pill of a port before it moves to the top of its
 * controller instead (and stays at the bottom if the top is taken too). out[i].w is 0 for a port without a label. */
void pad_label_layout_set(int n, const float* pad_x, const float* pad_y, const float* text_w, int centred, pad_label_rect* out);
