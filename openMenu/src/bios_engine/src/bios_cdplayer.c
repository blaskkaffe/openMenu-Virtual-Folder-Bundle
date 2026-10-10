/* bios_cdplayer: see bios_cdplayer.h. Objects, ids, positions and selection effects are those of
 * music_screen_update (0x8C018BC8), cdplayer_cursor_cb (0x8C018FC2) and cdplayer_display_update (0x8C019220). */
#include "bios_cdplayer.h"

#define PRIO 0x1000
#define ID_BACK 0x1110
#define ID_DISC 0x1200
#define ID_REPEAT 0x1340
#define ID_DIGIT(i) ((uint16_t)(0x1310 + (i))) /* 0x1310..0x1319, scripts 0x2A..0x33 */
#define BUTTON_Z (-351.5625f)
#define BUTTON_Y (-14.84375f)

/* The object of each cursor position (the BIOS numbers them left to right; the stop and play buttons swap ids). */
static const uint16_t item_id[BCD_ITEMS] = {ID_BACK, 0x1301, 0x1303, 0x1302, 0x1304, 0x1305};
/* x of each item (BACK as music_screen_update places it, the buttons from scripts 0x17..0x1B) */
static const float item_x[BCD_ITEMS] = {-19.53125f, -11.953125f, -4.0234375f, 3.90625f, 11.8359375f, 19.765625f};

/* The cursor table 0x8C038610: left / right per position (up / down stay put). */
static const signed char nav_table[BCD_ITEMS][2] = {{5, 1}, {0, 2}, {1, 3}, {2, 4}, {3, 5}, {4, 0}};

void
bcd_open(bcdplayer* c, bmenu* m, int disc, int tracks, int seconds) {
    c->m = m;
    c->cursor = BCD_PLAY;
    c->anim = 0;
    c->state = 0;
    c->repeat = 0;
    c->disc = disc;
    c->show = disc ? BCD_SHOW_TRACKS : BCD_SHOW_BLANK;
    c->track = disc ? tracks : 0;
    c->seconds = disc ? seconds : 0;
    bvm* vm = &m->vm;
    bvm_kill_all(vm);
    bvm_create(vm, 6, ID_BACK, PRIO);
    bvm_create(vm, 0x17, 0x1301, PRIO);
    bvm_create(vm, 0x18, 0x1302, PRIO);
    bvm_create(vm, 0x19, 0x1303, PRIO);
    bvm_create(vm, 0x1a, 0x1304, PRIO);
    bvm_create(vm, 0x1b, 0x1305, PRIO);
    bvm_create(vm, 0x1c, ID_DISC, PRIO + 1);
    bvm_create(vm, 0x47, ID_REPEAT, PRIO);
    bvm_obj* back = bvm_find(vm, ID_BACK);
    if (back) {
        back->pos_tw[0].cur = item_x[BCD_BACK];
        back->pos_tw[1].cur = BUTTON_Y;
    }
    for (int i = 0; i < 10; i++) {
        bvm_create(vm, 0x2a + i, ID_DIGIT(i), PRIO);
    }
}

int
bcd_set_cursor(bcdplayer* c, int item) {
    if (item < 0 || item >= BCD_ITEMS || item == c->cursor) {
        return 0;
    }
    c->cursor = item;
    return 1;
}

int
bcd_nav(bcdplayer* c, int dir) {
    if (dir != BCD_LEFT && dir != BCD_RIGHT) {
        return 0;
    }
    return bcd_set_cursor(c, nav_table[c->cursor][dir == BCD_RIGHT]);
}

int
bcd_press(bcdplayer* c, int item) {
    if (item < BCD_PREV || item >= BCD_ITEMS) {
        return 0;
    }
    bvm_obj* o = bvm_find(&c->m->vm, item_id[item]);
    if (o) {
        o->var[1] = 1; /* the button's own script makes it jump (track 1 of 0x8C06FC74) */
    }
    return 1;
}

void
bcd_set_display(bcdplayer* c, int show, int track, int seconds) {
    c->show = show;
    c->track = track;
    c->seconds = seconds;
}

void
bcd_set_repeat(bcdplayer* c, int mode) {
    c->repeat = mode < 0 || mode > 2 ? 0 : mode;
}

static void
digit(bvm* vm, int i, int value) {
    bvm_obj* o = bvm_find(vm, ID_DIGIT(i));
    if (o) {
        o->model = (uint16_t)(0x15 + value); /* the digit models 21..30 */
        o->texlist = o->model;
    }
}

void
bcd_sync(bcdplayer* c) {
    bvm* vm = &c->m->vm;
    c->anim++;
    if (c->state == 0) {
        /* state 1 of music_screen_update: the play button lit; then the drive's "disc ready" event shows the disc */
        bvm_obj* disc = bvm_find(vm, ID_DISC);
        if (disc && c->disc) {
            disc->var[0] = 2; /* comes up from below and spins (script 0x1C) */
        }
        c->state = 1;
    }
    /* cdplayer_cursor_cb: the selected button var0 = 1, the others 0; BACK var0 = 1 while it is selected */
    for (int i = BCD_PREV; i < BCD_ITEMS; i++) {
        bvm_obj* o = bvm_find(vm, item_id[i]);
        if (o) {
            o->var[0] = i == c->cursor;
        }
    }
    bvm_obj* back = bvm_find(vm, ID_BACK);
    if (back) {
        back->var[0] = c->cursor == BCD_BACK;
    }
    bvm_obj* rep = bvm_find(vm, ID_REPEAT);
    if (rep) {
        rep->var[0] = c->repeat;
    }

    /* cdplayer_display_update: TRACK tt and TIME (h)mm:ss as digit models; the hundreds of minutes hide at 0 */
    int track = c->show == BCD_SHOW_BLANK ? 0 : c->track;
    int t = c->show == BCD_SHOW_BLANK || c->show == BCD_SHOW_SELECTED ? 0 : c->seconds;
    if (track < 0) track = 0;
    if (t < 0) t = 0;
    digit(vm, 1, track % 10);
    digit(vm, 2, (track / 10) % 10);
    digit(vm, 5, (t / 6000) % 10);
    digit(vm, 6, (t / 600) % 10);
    digit(vm, 7, (t / 60) % 10);
    digit(vm, 8, (t / 10) % 6);
    digit(vm, 9, t % 10);
    bvm_obj* h = bvm_find(vm, ID_DIGIT(5));
    if (h) {
        h->var[0] = (t / 6000) % 10 == 0;
    }
}

void
bcd_draw(bcdplayer* c, const bscene_sink* sink) {
    static const unsigned passes[2] = {BSCENE_PART_MODEL, BSCENE_PART_TEXT};
    bmenu* m = c->m;
    bmenu_back_style(m, c->cursor == BCD_BACK, c->anim, c->pal);
    for (int pass = 0; pass < 2; pass++) {
        m->scene.parts = passes[pass];
        for (int i = 0; i < m->vm.count; i++) {
            bscene_draw_object(&m->scene, &m->vm.objs[m->vm.order[i]], sink);
        }
    }
    m->scene.parts = BSCENE_PART_ALL;
}

void
bcd_item_px(int item, float* x, float* y) {
    item = item < 0 ? 0 : (item >= BCD_ITEMS ? BCD_ITEMS - 1 : item);
    float px = BSCENE_FOCAL / -BUTTON_Z;
    *x = 320.0f + item_x[item] * px;
    *y = 240.0f - BUTTON_Y * px;
}

int
bcd_item_at_px(float x, float y) {
    for (int i = 0; i < BCD_ITEMS; i++) {
        float cx, cy;
        bcd_item_px(i, &cx, &cy);
        if (x >= cx - 40.0f && x <= cx + 40.0f && y >= cy - 40.0f && y <= cy + 40.0f) {
            return i;
        }
    }
    return -1;
}
