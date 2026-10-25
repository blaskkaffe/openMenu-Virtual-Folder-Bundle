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
    {"fish", "Fishing Rod"},
    {"maraca", "Maracas"},
    {"samba", "Maracas"},
    {"twin stick", "Twin Stick"},
    {"twin-stick", "Twin Stick"},
    {"arcade", "Arcade Stick"},
    {"fighting stick", "Arcade Stick"},
    {"racing", "Racing Wheel"},
    {"wheel", "Racing Wheel"},
    {"densha", "Train Controller"},
    {"train", "Train Controller"},
    {"panther", "Panther"},
    {"dance", "Dance Mat"},
    {"dreameye", "Dreameye"},
    {"camera", "Dreameye"},
    {"karaoke", "Karaoke Mic"},
    {"microphone", "Microphone"},
    {"gun", "Light Gun"},
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
        snprintf(out, PAD_KIND_MAX, "%s", (functions & FN_MOUSE) ? "Keyboard + Mouse" : "Keyboard");
        return out;
    }
    if (functions & FN_MOUSE) {
        snprintf(out, PAD_KIND_MAX, "Mouse");
        return out;
    }
    if (functions & (FN_LIGHTGUN | FN_ARGUN)) {
        snprintf(out, PAD_KIND_MAX, "Light Gun");
        return out;
    }
    if (functions & FN_CAMERA) {
        snprintf(out, PAD_KIND_MAX, "Dreameye");
        return out;
    }
    if (functions & FN_MICROPHONE && !(functions & FN_CONTROLLER)) {
        snprintf(out, PAD_KIND_MAX, "Microphone");
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
#define PILL_H 20.0f
#define PILL_PAD 8.0f /* left and right of the text */

void
pad_label_layout(float pad_x, float pad_y, float text_w, pad_label_rect* out) {
    out->w = text_w + 2.0f * PILL_PAD;
    out->h = PILL_H;
    out->x = pad_x + PAD_RIGHT - out->w;
    out->y = pad_y + PAD_BOTTOM - out->h;
}
