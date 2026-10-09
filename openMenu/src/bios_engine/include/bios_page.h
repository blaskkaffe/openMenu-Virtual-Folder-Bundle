/*
 * bios_page: the look of the BIOS Settings screen (rows with a 3D icon, a pill and a text line,
 * plus a help box underneath) as a scrollable list. The original shows four fixed rows; this
 * shows four at a time and scrolls through any number of rows. It only places objects; what
 * the rows say and do is up to the caller. Portable C.
 *
 * Object ids and scripts are those of settings_screen_update in the original.
 */
#ifndef BIOS_PAGE_H
#define BIOS_PAGE_H

#include "bios_menu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BPAGE_SLOTS 4

/* Row icons: the four BIOS settings icons, or a digit 0-9 as a placeholder. */
#define BPAGE_ICON_BIOS(i) (i)        /* 0 language, 1 date/time, 2 sound, 3 other */
#define BPAGE_ICON_DIGIT(d) (10 + (d)) /* 3D digit model, until real icons exist */

/* Text object ids: slot rows 0x1407..0x140A, help box 0x140B. The text of a row is
 * "label\tvalue" (value right aligned); the help box holds a plain string. */
#define BPAGE_TEXT_ID(slot) (0x1407 + (slot))
#define BPAGE_HELP_ID 0x140B
#define BPAGE_LABEL_MAX 96 /* room for a text line: label, tab, value */

typedef struct bpage {
    bmenu* m;
    int count;  /* rows in the list */
    int cursor; /* selected row */
    int top;    /* first visible row */
    int back_selected; /* the cursor is on the BACK marker, one step down from the last row */
    int anim;   /* frames since the page opened, for the BACK marker's blink */
    int pal;    /* PAL console: the BACK arrow colour */
} bpage;

typedef struct bpage_row {
    int icon;
} bpage_row;

typedef void (*bpage_row_fn)(void* user, int index, bpage_row* out);

/* Replace the scene by the settings page with `count` rows. */
void bpage_open(bpage* p, bmenu* m, int count);

/* Move the selection by `delta` rows (clamped). Returns 1 if it moved. */
int bpage_move(bpage* p, int delta);

/* Put the cursor on a row (the mouse). Returns 1 if it moved. */
int bpage_set_cursor(bpage* p, int row);

/* Apply selection, icons and visibility; call once per frame after bmenu_update(). */
void bpage_sync(bpage* p, bpage_row_fn row, void* user);

/* Is the pixel on the BACK marker (bottom left)? */
int bpage_back_at_px(float x, float y);

/* Row shown in slot `slot`, -1 if the slot is empty. */
int bpage_row_in_slot(const bpage* p, int slot);

/* The slot whose bar is under a point (pixels), -1 if none. */
int bpage_slot_at_px(const bpage* p, float x, float y);

/* Draw the objects of the page (no background). */
void bpage_draw(bpage* p, const bscene_sink* sink);

#ifdef __cplusplus
}
#endif

#endif /* BIOS_PAGE_H */
