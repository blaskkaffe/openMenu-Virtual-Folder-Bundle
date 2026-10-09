/*
 * bios_list: a dense version of the BIOS Settings look for browsing games. Five or seven rows
 * (a pill, a 3D disc and a text line each) take about 60% of the screen width, leaving room on
 * the right for the box art. The selected row's disc spins like the CD player's. A launch
 * animation moves the rows out in a circle and the selected disc to the CD player's position.
 * Portable C.
 */
#ifndef BIOS_LIST_H
#define BIOS_LIST_H

#include "bios_menu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLIST_MAX_SLOTS 7

/* Text object ids of the rows (left aligned, one string each). */
#define BLIST_TEXT_ID(slot) (0x17C0 + (slot))
#define BLIST_TEXT_FIRST 0x17C0
/* The disc number of a multi-disc game ("2:4") in the small pill at the right end of its row. */
#define BLIST_NUM_ID(slot) (0x17D0 + (slot))
#define BLIST_TEXT_LAST (0x17D0 + BLIST_MAX_SLOTS - 1)

/* Row disc textures: the disc of slot `s` uses texlist BLIST_TEXLIST_BASE + s; its texture 0
 * is the label (the renderer substitutes the game's art), the others are those of the disc. */
#define BLIST_TEXLIST_BASE 0x1000
#define BLIST_DISC_MODEL 61

/* Screen area of a row's text surface (pixels), for the renderer. */
#define BLIST_TEXT_W 300
#define BLIST_TEXT_H 32
#define BLIST_NUM_W 48

#define BLIST_BUTTONS 5

typedef struct blist {
    bmenu* m;
    int slots; /* rows shown: 5 or 7 */
    int count; /* rows in the list */
    int cursor;
    int top;
    int launching;
    int launch_frame;
    int back_selected; /* the cursor is on the BACK marker, below the last row */
    int pal_console;   /* set by the caller: the console is a PAL one (the BACK marker is blue instead of red) */
    float base[BLIST_BUTTONS + 1][3]; /* resting positions of the five buttons and the BACK marker (for the launch animation) */
    int base_valid;
    unsigned anim;     /* frames since the list was opened (the BACK marker's frame blinks) */
    int multi[BLIST_MAX_SLOTS]; /* set by the caller before blist_sync(): row is a multi-disc set */
} blist;

void blist_open(blist* l, bmenu* m, int slots, int count);
void blist_set_count(blist* l, int count); /* the list changed; keeps the cursor in range */
int blist_move(blist* l, int delta);       /* returns 1 if the cursor moved */
void blist_goto(blist* l, int cursor);        /* cursor there, list scrolled to put it mid-screen */
void blist_set_cursor(blist* l, int cursor); /* cursor there, scrolling only as far as needed */
int blist_row_in_slot(const blist* l, int slot); /* -1 if empty */

/* Place and light the objects; call once per frame after bmenu_update(). */
void blist_sync(blist* l);
void blist_draw(blist* l, const bscene_sink* sink);

/* Launch animation: start it, then call step once per logic frame until it returns 1. */
void blist_launch_start(blist* l);
int blist_launch_step(blist* l);
void blist_launch_cancel(blist* l);

/* The five buttons of the CD player along the bottom, without their pictures. The caller writes
 * text on them: this gives the centre of button i (pixels). */
void blist_button_center_px(int i, float* x, float* y);

/* The BACK marker is under a point (pixels). */
int blist_back_at_px(float x, float y);

/* The slot whose bar is under a point (pixels), -1 if none; and the buttons row. */
int blist_slot_at_px(const blist* l, float x, float y);

/* Right edge of the rows (pixels). */
float blist_row_right_px(void);
/* Top of the first and bottom of the last row bar in pixels (what a side panel lines up with). */
void blist_rows_extent_px(const blist* l, float* top, float* bottom);

#ifdef __cplusplus
}
#endif

#endif /* BIOS_LIST_H */
