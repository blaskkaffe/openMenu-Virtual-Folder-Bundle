/*
 * bios_page: the look of the BIOS Settings screen (rows with a 3D icon, a pill and a text line) as a scrollable
 * list. The original shows four rows, each on a bar of its own, with a help box underneath. This has the game
 * list's one tall box (the row bar model grown along y, see bios_list) as wide as the original's rows, holding
 * BPAGE_SLOTS rows, and a second, separate bar under it for the information about the selected row. The caller
 * writes the information text inside the rectangle bpage_info_rect_px() gives. Green arrows (the date editor's)
 * at the box's right edge show that there are more rows above / below. It only places objects; what the rows say
 * and do is up to the caller. Portable C.
 */
#ifndef BIOS_PAGE_H
#define BIOS_PAGE_H

#include "bios_menu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BPAGE_SLOTS 5

/* Row icons: the four BIOS settings icons (with their idle motion), a digit 0-9 as a placeholder, or another of the BIOS's models, or one of
 * bios_models.h, drawn without motion and scaled to the row. The numbers are also those of UIS_ICON_* in ui_settings.h. */
#define BPAGE_ICON_BIOS(i) (i)        /* 0 language (lips), 1 date/time (clock), 2 sound (speaker), 3 other (question mark) */
#define BPAGE_ICON_DIGIT(d) (10 + (d)) /* 3D digit model, until real icons exist */
#define BPAGE_ICON_VMU 20              /* the memory card / VMU (ROM model 62) */
#define BPAGE_ICON_PAD 21              /* the controller (ROM model 17) */
#define BPAGE_ICON_GLOBE 22            /* the globe (built in) */
#define BPAGE_ICON_PHONE 23            /* the telephone (built in) */
#define BPAGE_ICON_CASE 24             /* the CD jewel case (built in) */
#define BPAGE_ICON_DISC 25             /* the GD-ROM disc (ROM model 61) */

/* Where the text goes (pixels), copied from the original Settings screen (photo of the console): the label is centred between the icon
 * (which ends at x 100) and the value pill, the value starts 10 px inside the pill, left aligned, both centred on the row's height. */
#define BPAGE_PILL_LEFT_PX 304.0f /* the original's is 256.6; moved right so that a label of 17 characters fits before it */
#define BPAGE_LABEL_CX_PX ((100.0f + BPAGE_PILL_LEFT_PX) * 0.5f)
#define BPAGE_VALUE_X_PX (BPAGE_PILL_LEFT_PX + 10.0f) /* the help text starts here too, as in the original */

/* Text object ids of the slot rows. The text of a row is "label\tvalue" (the value in a column of its own). */
#define BPAGE_TEXT_ID(slot) (0x1407 + (slot))
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

#define BPAGE_UP 0
#define BPAGE_DOWN 1
#define BPAGE_LEFT 2
#define BPAGE_RIGHT 3
/* D-pad exactly as the BIOS Settings page's cursor table (0x8C037D70) has it. Returns 1 if the cursor moved. */
int bpage_nav(bpage* p, int dir);

/* Is the pixel on the BACK marker (bottom left)? */
int bpage_back_at_px(float x, float y);

/* Row shown in slot `slot`, -1 if the slot is empty. */
int bpage_row_in_slot(const bpage* p, int slot);

/* The slot whose bar is under a point (pixels), -1 if none. */
int bpage_slot_at_px(const bpage* p, float x, float y);

/* The scroll bar's thumb (pixels): a green rectangle as wide as the arrows are halfway up, moving between them as the cursor goes
 * down the list. Returns 0 when everything fits (no scroll bar). Draw it in the arrows' colour BPAGE_THUMB_ARGB over the pills. */
#define BPAGE_THUMB_ARGB 0xFF629B6Eu
int bpage_scroll_thumb_px(const bpage* p, float* x, float* y, float* w, float* h);

/* The rectangle (pixels) inside the information bar that the caller's text must stay within: x0, y0, x1, y1. */
void bpage_info_rect_px(float* x0, float* y0, float* x1, float* y1);

/* Draw the objects of the page (no background). */
void bpage_draw(bpage* p, const bscene_sink* sink);

#ifdef __cplusplus
}
#endif

#endif /* BIOS_PAGE_H */
