/*
 * bios_case: the CD case shown for the selected game. A new case flies in from the side the selection
 * came from while the old one flies out the opposite way; at rest the cases tilt slowly about all three
 * axes, each game with its own phase. Portable, one bcase_step() per displayed frame.
 */
#ifndef BIOS_CASE_H
#define BIOS_CASE_H

#include "bios_scene.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BCASE_TAG_MAX 16
#define BCASE_FLY_DOWN 480.0f /* a case leaving at the bottom ends this far below its resting place: off the screen */
#define BCASE_FLY_UP 260.0f   /* ... and this far above it */
#define BCASE_FLY_PX 300.0f   /* scale of the lean while flying */

typedef struct bcase_item {
    int active;
    int model;                 /* BMODEL_CASE_* */
    char tag[BCASE_TAG_MAX];   /* what the renderer needs to find the picture (the product code) */
    float y;                   /* offset from the resting place in px, + is down */
    float target;              /* where it is heading */
    float phase[3];
    float sign[3];
    int leaving;
} bcase_item;

typedef struct bcase {
    bcase_item cur, old;
    unsigned frame;
} bcase;

void bcase_init(bcase* c);

/* Show the case of `model` (-1: none) with picture tag `tag`. dir > 0 means the selection moved down
 * (the new case comes from the top, the old one leaves at the bottom), dir < 0 the other way.
 * Asking for what is already shown does nothing. */
void bcase_show(bcase* c, int model, const char* tag, int dir);

void bcase_step(bcase* c);

/* Called before a case is drawn so the renderer can bind its picture. */
typedef void (*bcase_bind_fn)(void* user, const char* tag);

/* Draw both cases centred on (cx, cy); `size_px` is the height of a case at rest. */
void bcase_draw(const bcase* c, bscene* s, float cx, float cy, float size_px, bcase_bind_fn bind, void* user,
                const bscene_sink* sink);

#ifdef __cplusplus
}
#endif

#endif /* BIOS_CASE_H */
