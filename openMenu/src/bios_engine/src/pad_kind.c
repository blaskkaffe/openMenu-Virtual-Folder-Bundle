/* pad_kind: see pad_kind.h. */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "pad_kind.h"

/* Maple function codes (the bits of a device's function mask) */
#define FN_CONTROLLER 0x01000000u
#define FN_MICROPHONE 0x10000000u
#define FN_ARGUN 0x20000000u
#define FN_KEYBOARD 0x40000000u
#define FN_LIGHTGUN 0x80000000u
#define FN_MOUSE 0x00020000u
#define FN_CAMERA 0x00080000u

/* Product names of the official and the common third party devices, by a word in them (case does not matter), and what to call
 * them. The first match wins: the more specific words come first. The product name is what the device itself reports. */
static const struct {
    const char* word;
    const char* name;
} known[] = {
    {"fish", "Fishing"},
    {"maraca", "Maracas"},
    {"samba", "Maracas"},
    {"twin stick", "Twin"},
    {"twin-stick", "Twin"},
    {"arcade", "Arcade"},
    {"fighting stick", "Arcade"},
    {"racing", "Racing"},
    {"wheel", "Racing"},
    {"densha", "Train"},
    {"train", "Train"},
    {"panther", "Panther"},
    {"pop'n", "Pop'n Music"},
    {"popn", "Pop'n Music"},
    {"dance", "Dance"},
    {"dreameye", "Dreameye"},
    {"camera", "Dreameye"},
    {"karaoke", "Karaoke"},
    {"microphone", "Mic"},
    {"gun", "Lightgun"},
    {"keyboard", "Keyboard"},
    {"mouse", "Mouse"},
};

static int
contains(const char* hay, const char* word) {
    size_t n = strlen(word);
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i] && tolower((unsigned char)hay[i]) == word[i]) {
            i++;
        }
        if (i == n) {
            return 1;
        }
    }
    return 0;
}

/* the product name as it can be shown: no leading "Dreamcast", no blanks around, cut to fit */
static void
tidy(const char* name, char out[PAD_KIND_MAX]) {
    while (*name == ' ') {
        name++;
    }
    if (strncmp(name, "Dreamcast ", 10) == 0) {
        name += 10;
    }
    snprintf(out, PAD_KIND_MAX, "%s", name);
    for (int i = (int)strlen(out) - 1; i >= 0 && (out[i] == ' ' || out[i] == '\0'); i--) {
        out[i] = '\0';
    }
}

const char* pad_kind_name(uint32_t functions, const char* product_name, char out[PAD_KIND_MAX]) {
    const char* name = product_name ? product_name : "";
    for (unsigned i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
        if (contains(name, known[i].word)) {
            snprintf(out, PAD_KIND_MAX, "%s", known[i].name);
            return out;
        }
    }
    /* nothing in the name: the functions say what it is */
    if (functions & FN_KEYBOARD) {
        snprintf(out, PAD_KIND_MAX, "Keyboard");
        return out;
    }
    if (functions & FN_MOUSE) {
        snprintf(out, PAD_KIND_MAX, "Mouse");
        return out;
    }
    if (functions & (FN_LIGHTGUN | FN_ARGUN)) {
        snprintf(out, PAD_KIND_MAX, "Lightgun");
        return out;
    }
    if (functions & FN_CAMERA) {
        snprintf(out, PAD_KIND_MAX, "Dreameye");
        return out;
    }
    if (functions & FN_MICROPHONE && !(functions & FN_CONTROLLER)) {
        snprintf(out, PAD_KIND_MAX, "Mic");
        return out;
    }
    if (functions & FN_CONTROLLER) {
        char t[PAD_KIND_MAX];
        tidy(name, t);
        if (t[0] == '\0' || strcmp(t, "Controller") == 0) {
            return NULL; /* the standard controller */
        }
        snprintf(out, PAD_KIND_MAX, "%s", t);
        return out;
    }
    if (name[0]) {
        tidy(name, out);
        return out;
    }
    snprintf(out, PAD_KIND_MAX, "Unknown");
    return out;
}

/* The controller picture of the grid is about 66 x 62 pixels around the position of its object (measured on the BIOS's grid). */
#define PAD_RIGHT 35.0f  /* its right edge, from the position */
#define PAD_BOTTOM 28.0f /* its bottom edge */
#define PILL_H (26.0f * PAD_LABEL_SCALE + 8.0f) /* a line of the font (26 px) and 4 px above and below */
#define PILL_PAD 6.0f /* left and right of the text */

void
pad_label_layout(float pad_x, float pad_y, float text_w, int centred, pad_label_rect* out) {
    out->w = text_w + 2.0f * PILL_PAD;
    out->h = PILL_H;
    out->x = centred ? pad_x - out->w / 2.0f : pad_x + PAD_RIGHT - out->w;
    out->y = pad_y + PAD_BOTTOM - out->h;
}

#define PAD_TOP 42.0f /* the pill of a port that has to give way: its top edge this far above the controller's position, so it clears the bottom row */
#define PILL_GAP 2.0f /* pills closer than this count as touching */

static int
touches(const pad_label_rect* a, const pad_label_rect* b) {
    return b->w > 0.0f && a->x < b->x + b->w + PILL_GAP && b->x < a->x + a->w + PILL_GAP && a->y < b->y + b->h + PILL_GAP &&
           b->y < a->y + a->h + PILL_GAP;
}

void
pad_label_layout_set(int n, const float* pad_x, const float* pad_y, const float* text_w, int centred, pad_label_rect* out) {
    for (int i = 0; i < n; i++) {
        out[i] = (pad_label_rect){0.0f, 0.0f, 0.0f, 0.0f};
        if (text_w[i] <= 0.0f) {
            continue;
        }
        pad_label_layout(pad_x[i], pad_y[i], text_w[i], centred, &out[i]); /* the bottom */
        int hit = 0;
        for (int j = 0; j < i; j++) {
            hit |= touches(&out[i], &out[j]);
        }
        if (!hit) {
            continue;
        }
        pad_label_rect top = out[i];
        top.y = pad_y[i] - PAD_TOP;
        int top_hit = 0;
        for (int j = 0; j < i; j++) {
            top_hit |= touches(&top, &out[j]);
        }
        if (!top_hit) {
            out[i] = top;
        }
    }
}
