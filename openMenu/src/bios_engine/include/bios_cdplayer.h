/*
 * bios_cdplayer: the BIOS Music screen (the CD player) as music_screen_update (0x8C018BC8) builds it: the BACK
 * marker, the five buttons, the disc, the repeat indicator and the TRACK / TIME readout made of 3D digits, with the
 * BIOS cursor table (0x8C038610) and the same selection effects. Nothing is played: the caller decides what the
 * buttons do. Portable C.
 */
#ifndef BIOS_CDPLAYER_H
#define BIOS_CDPLAYER_H

#include "bios_menu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Cursor positions, as the BIOS numbers them (left to right on screen). */
enum {
    BCD_BACK = 0,
    BCD_PREV = 1,
    BCD_STOP = 2,
    BCD_PLAY = 3, /* play / pause: where the cursor starts */
    BCD_NEXT = 4,
    BCD_REPEAT = 5,
    BCD_ITEMS = 6
};

/* What the readout shows (cdplayer_display_update 0x8C019220). */
enum {
    BCD_SHOW_BLANK = 0x0C,    /* 00  00:00 (no disc) */
    BCD_SHOW_TRACKS = 0x0D,   /* number of tracks and the disc's length */
    BCD_SHOW_PLAYING = 0x0E,  /* current track and elapsed time */
    BCD_SHOW_SELECTED = 0x0F  /* the selected track, time 00:00 */
};

typedef struct bcdplayer {
    bmenu* m;
    int cursor;
    int anim;
    int pal;
    int state;   /* 0 just opened, 1 the objects are in place */
    int disc;    /* a disc is shown (spinning) */
    int show;    /* BCD_SHOW_* */
    int track;   /* number shown in the TRACK field */
    int seconds; /* time shown in the TIME field */
    int repeat;  /* 0 off, 1 one track, 2 all (the indicator next to TRACK) */
} bcdplayer;

/* Replace the scene by the CD player with the cursor on Play / Pause. `disc` shows the disc (as when the drive
 * reports one: event 0x34), with the TRACK / TIME fields showing `tracks` and `seconds`. */
void bcd_open(bcdplayer* c, bmenu* m, int disc, int tracks, int seconds);

/* D-pad: left / right step along BACK and the five buttons (wrapping), up / down do nothing (cursor table
 * 0x8C038610). Returns 1 if the cursor moved. */
#define BCD_LEFT 0
#define BCD_RIGHT 1
int bcd_nav(bcdplayer* c, int dir);
int bcd_set_cursor(bcdplayer* c, int item);

/* A on a button (not BACK): the button jumps as in the BIOS (var1 = 1, its script's track 1). Returns 1 if `item`
 * is a button; the BIOS plays the confirm sound then. */
int bcd_press(bcdplayer* c, int item);

/* Readout and repeat indicator (the BIOS sets these from the drive state). */
void bcd_set_display(bcdplayer* c, int show, int track, int seconds);
void bcd_set_repeat(bcdplayer* c, int mode);

/* Once per frame after bmenu_update(): selection, readout digits. */
void bcd_sync(bcdplayer* c);
void bcd_draw(bcdplayer* c, const bscene_sink* sink);

/* Screen position (pixels) of the centre of item `item` and the item under a pixel (-1 if none), for a mouse. */
void bcd_item_px(int item, float* x, float* y);
int bcd_item_at_px(float x, float y);

#ifdef __cplusplus
}
#endif

#endif /* BIOS_CDPLAYER_H */
