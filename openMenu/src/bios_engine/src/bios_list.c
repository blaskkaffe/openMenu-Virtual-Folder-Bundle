/* bios_list: see bios_list.h. */
#include "bios_list.h"

#include <math.h>

#define PRIO 0x1000
#define PX_PER_UNIT 11.32f /* 4000 / 353.5: pixels per world unit at the rows' depth */
#define ROW_Z (-353.515625f)

/* Layout in world units (screen centre = 0,0). */
#define ROW_SY 1.0f         /* same height as the rows of the settings screen (about 53 px) */
#define ANCHOR_X 1.95f      /* row bar origin, as in the Settings screen */
/* The row bar (model 41) is 59 units wide: a round left end (to x=-28), a straight middle and a
 * small right end (from 27.5). The middle is shortened so the bar is about 60% of the screen
 * wide while both ends keep their shape. */
#define BAR_CAP_L (-28.0f)
#define BAR_CAP_R 27.5f
#define BAR_MIDDLE_F 0.638f
#define ICON_X (-22.6f)
#define ICON_SCALE 0.125f /* the disc is as big as the Settings icons (about 40 px) */
#define TEXT_X (-4.8f)      /* centre of the 300 px text surface */
/* The lit part of the row (model 33, 36.9 units wide with 0.4 chamfered ends) runs from just left of
 * the text to the right end of the bar; its middle is shortened so the ends keep their shape. */
#define PILL_X (-5.45f)
#define PILL_CAP_L (-17.1f)
#define PILL_CAP_R 19.0f
#define PILL_F 0.9086f
#define PILL_SCALE 0.859375f
/* disc selector of multi-disc games: a small pill at the right end of the row, with the shiny
 * side of a CD and the disc number */
#define SPILL_X 16.98f
#define SPILL_F 0.183f
#define MINI_X 3.3f
#define MINI_SCALE 0.0625f
#define NUM_TEXT_X 7.9f /* the text script offsets its text by -1.37 */
/* the BACK marker and the five buttons along the bottom are where the CD player has them */
#define BACK_X (-19.53125f)
#define BACK_Y (-14.84375f)
#define BACK_Z (-351.5625f)
#define BACK_SCALE 1.0f
#define BUTTON_Y (-14.84375f)
#define ID_BUTTON(i) ((uint16_t)(0x1301 + (i)))
#define Y_CENTER 2.47f /* the rows sit above the buttons: centred at y = 212 px */

/* CD player disc (script 0x1c): position and spin per frame, in the original's angle units */
#define CD_Z (-378.90625f)
#define CD_SCALE 0.875f
#define SPIN_Y 140 /* 0.77 degrees: 0x10000 = 360 degrees */
#define SPIN_Z 500 /* 2.75 degrees */

#define LAUNCH_FRAMES 26
#define LAUNCH_HOLD 6

static const struct {
    int anchor_script, pill_script, icon_script, text_script;
} proto = {0x49, 0x1d, 0x4d, 0x23};

#define ID_ANCHOR(s) ((uint16_t)(0x1700 + (s)))
#define ID_PILL(s) ((uint16_t)(0x1720 + (s)))
#define ID_ICON(s) ((uint16_t)(0x1740 + (s)))
#define ID_SPILL(s) ((uint16_t)(0x1760 + (s)))
#define ID_MINI(s) ((uint16_t)(0x1780 + (s)))

void
blist_open(blist* l, bmenu* m, int slots, int count) {
    l->m = m;
    l->slots = slots < 1 ? 1 : (slots > BLIST_MAX_SLOTS ? BLIST_MAX_SLOTS : slots);
    l->cursor = 0;
    l->top = 0;
    l->launching = 0;
    l->launch_frame = 0;
    bvm_kill_all(&m->vm);
    for (int s = 0; s < l->slots; s++) {
        bvm_create(&m->vm, proto.anchor_script, ID_ANCHOR(s), PRIO);
    }
    for (int s = 0; s < l->slots; s++) {
        bvm_create(&m->vm, proto.pill_script, ID_PILL(s), PRIO);
    }
    for (int s = 0; s < l->slots; s++) {
        bvm_create(&m->vm, proto.icon_script, ID_ICON(s), PRIO);
    }
    for (int s = 0; s < l->slots; s++) {
        bvm_create(&m->vm, 0x1e, ID_SPILL(s), PRIO);
    }
    for (int s = 0; s < l->slots; s++) {
        bvm_create(&m->vm, 0x4e, ID_MINI(s), PRIO);
    }
    for (int s = 0; s < l->slots; s++) {
        bvm_create(&m->vm, proto.text_script, (uint16_t)BLIST_TEXT_ID(s), PRIO);
        bvm_create(&m->vm, 0x24, (uint16_t)BLIST_NUM_ID(s), PRIO);
    }
    bvm_create(&m->vm, 6, 0x1110, PRIO); /* BACK marker */
    for (int i = 0; i < BLIST_BUTTONS; i++) {
        bvm_create(&m->vm, 0x17 + (i == 1 ? 2 : (i == 2 ? 1 : i)), ID_BUTTON(i), PRIO); /* scripts 0x17,0x19,0x18,0x1a,0x1b: left to right */
    }
    for (int s = 0; s < BLIST_MAX_SLOTS; s++) {
        l->multi[s] = 0;
    }
    blist_set_count(l, count);
}

void
blist_set_count(blist* l, int count) {
    l->count = count < 0 ? 0 : count;
    if (l->cursor > l->count - 1) {
        l->cursor = l->count > 0 ? l->count - 1 : 0;
    }
    if (l->top > l->cursor) {
        l->top = l->cursor;
    }
    if (l->count <= l->slots) {
        l->top = 0;
    }
}

static void
fix_top(blist* l) {
    if (l->cursor < l->top) {
        l->top = l->cursor;
    }
    if (l->cursor >= l->top + l->slots) {
        l->top = l->cursor - l->slots + 1;
    }
    if (l->top < 0) {
        l->top = 0;
    }
}

int
blist_move(blist* l, int delta) {
    int next = l->cursor + delta;
    if (next > l->count - 1) {
        next = l->count - 1;
    }
    if (next < 0) {
        next = 0;
    }
    if (l->count <= 0 || next == l->cursor) {
        return 0;
    }
    l->cursor = next;
    fix_top(l);
    return 1;
}

void
blist_goto(blist* l, int cursor) {
    l->cursor = cursor;
    l->top = cursor > l->slots / 2 ? cursor - l->slots / 2 : 0;
    blist_set_count(l, l->count);
    fix_top(l);
}

void
blist_set_cursor(blist* l, int cursor) {
    l->cursor = cursor < 0 ? 0 : (cursor > l->count - 1 ? (l->count > 0 ? l->count - 1 : 0) : cursor);
    fix_top(l);
    if (l->count <= l->slots) {
        l->top = 0;
    }
}

int
blist_row_in_slot(const blist* l, int slot) {
    int row = l->top + slot;
    return slot >= 0 && slot < l->slots && row < l->count ? row : -1;
}

void
blist_button_center_px(int i, float* x, float* y) {
    /* x of the buttons in the CD player's scripts 0x17, 0x19, 0x18, 0x1a, 0x1b */
    static const float bx[BLIST_BUTTONS] = {-11.953125f, -4.0234375f, 3.90625f, 11.8359375f, 19.765625f};
    *x = 320.0f + bx[i < 0 ? 0 : (i >= BLIST_BUTTONS ? BLIST_BUTTONS - 1 : i)] * PX_PER_UNIT;
    *y = 240.0f - BUTTON_Y * PX_PER_UNIT;
}

float
blist_row_right_px(void) {
    return 320.0f + (ANCHOR_X + (BAR_CAP_L + (BAR_CAP_R - BAR_CAP_L) * BAR_MIDDLE_F + 1.0f) * 0.8671875f) * PX_PER_UNIT;
}

/* Distance between rows in pixels: the bars are about 53 px high, so 6 rows have a 9 px gap and
 * 7 rows nearly touch; the rows are centred on the screen. */
static float
pitch_px(int slots) {
    switch (slots) {
        case 5: return 70.0f;
        case 6: return 58.0f;
        default: return 56.0f;
    }
}

static float
row_y(const blist* l, int s) {
    float pitch = pitch_px(l->slots) / PX_PER_UNIT;
    return Y_CENTER + ((float)(l->slots - 1) * 0.5f - (float)s) * pitch;
}

static void
set_pos(bvm_obj* o, float x, float y, float z) {
    o->flags &= ~(uint32_t)BVM_F_ATTACHED; /* absolute: the scripts attached these to the original's anchors */
    o->pos_tw[0].cur = x;
    o->pos_tw[1].cur = y;
    o->pos_tw[2].cur = z;
    o->pos_tw[0].step = o->pos_tw[1].step = o->pos_tw[2].step = 0;
    o->pos_tw[0].frames = o->pos_tw[1].frames = o->pos_tw[2].frames = 0;
    o->pos[0] = x;
    o->pos[1] = y;
    o->pos[2] = z;
}

static void
set_scale(bvm_obj* o, float sx, float sy, float sz) {
    o->scale_tw[0].cur = sx;
    o->scale_tw[1].cur = sy;
    o->scale_tw[2].cur = sz;
}

static void
set_hidden(bvm_obj* o, int hide) {
    o->flags = hide ? (o->flags | BVM_F_HIDE) : (o->flags & ~(uint32_t)BVM_F_HIDE);
}

static float
smooth(float t) {
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return t * t * (3.0f - 2.0f * t);
}

void
blist_launch_start(blist* l) {
    l->launching = 1;
    l->launch_frame = 0;
}

void
blist_launch_cancel(blist* l) {
    l->launching = 0;
    l->launch_frame = 0;
}

int
blist_launch_step(blist* l) {
    if (!l->launching) {
        return 0;
    }
    l->launch_frame++;
    return l->launch_frame >= LAUNCH_FRAMES + LAUNCH_HOLD;
}

/* Where a point of the row layout is while the rows leave in a circle (e = 0..1). */
static void
leave_xf(float e, float x, float y, float* ox, float* oy) {
    float phi = e * 3.6f;
    float k = 1.0f + e * 3.5f;
    float cs = cosf(phi), sn = sinf(phi);
    *ox = k * (cs * x - sn * y);
    *oy = k * (sn * x + cs * y);
}

void
blist_sync(blist* l) {
    bvm* vm = &l->m->vm;
    float t = l->launching ? (float)l->launch_frame / (float)LAUNCH_FRAMES : 0.0f;
    if (t > 1.0f) {
        t = 1.0f;
    }
    float e = smooth(t);
    int32_t rot_z = (int32_t)(e * 3.6f * 65536.0f / 6.2831853f);

    for (int s = 0; s < l->slots; s++) {
        int row = blist_row_in_slot(l, s);
        int on = row >= 0;
        int sel = on && row == l->cursor;
        int multi = on && l->multi[s];
        bvm_obj* anchor = bvm_find(vm, ID_ANCHOR(s));
        bvm_obj* pill = bvm_find(vm, ID_PILL(s));
        bvm_obj* icon = bvm_find(vm, ID_ICON(s));
        bvm_obj* spill = bvm_find(vm, ID_SPILL(s));
        bvm_obj* mini = bvm_find(vm, ID_MINI(s));
        bvm_obj* text = bvm_find(vm, (uint16_t)BLIST_TEXT_ID(s));
        bvm_obj* num = bvm_find(vm, (uint16_t)BLIST_NUM_ID(s));
        float y = row_y(l, s);
        float x, yy;
        int off = l->launching; /* the row is leaving the screen */

        /* the row bar and its parts: x of each part in the resting layout */
        struct {
            bvm_obj* o;
            float x, z;
            int show;
        } parts[4] = {
            {anchor, ANCHOR_X, ROW_Z, on},
            {pill, PILL_X, ROW_Z + 0.78f, sel},
            {spill, SPILL_X, ROW_Z + 0.78f, multi},
            {mini, MINI_X, ROW_Z + 1.2f, multi},
        };
        for (int i = 0; i < 4; i++) {
            if (!parts[i].o) {
                continue;
            }
            x = parts[i].x;
            yy = y;
            if (off) {
                leave_xf(e, x, y, &x, &yy);
            }
            set_pos(parts[i].o, x, yy, parts[i].z);
            parts[i].o->rot_tw[2].cur = off ? rot_z : 0;
            set_hidden(parts[i].o, !parts[i].show);
        }
        if (anchor) {
            set_scale(anchor, 0.8671875f, ROW_SY * 0.8671875f, 0.8671875f);
        }
        if (pill) {
            set_scale(pill, PILL_SCALE, ROW_SY * PILL_SCALE, PILL_SCALE);
            pill->var[1] = 1; /* the selected row is lit */
        }
        if (spill) {
            set_scale(spill, PILL_SCALE, ROW_SY * PILL_SCALE * 0.85f, PILL_SCALE);
            spill->var[1] = sel;
        }
        if (mini) {
            /* the shiny (reverse) side of a CD: the disc model turned half way round */
            mini->model = BLIST_DISC_MODEL;
            mini->texlist = BLIST_DISC_MODEL;
            mini->flags &= ~(uint32_t)BVM_F_MOTION;
            set_scale(mini, MINI_SCALE, MINI_SCALE, MINI_SCALE);
            mini->rot_tw[0].cur = 0;
            mini->rot_tw[1].cur = 0x8000;
            mini->rot_tw[0].step = mini->rot_tw[1].step = mini->rot_tw[2].step = 0;
        }

        if (icon) {
            icon->model = BLIST_DISC_MODEL;
            icon->texlist = (uint16_t)(BLIST_TEXLIST_BASE + s);
            icon->flags &= ~(uint32_t)BVM_F_MOTION;
            icon->var[0] = sel;
            float sc = ICON_SCALE;
            if (off && sel) {
                /* to the CD player's place, keeping the spin */
                float f = smooth(t);
                sc = ICON_SCALE + (CD_SCALE - ICON_SCALE) * f;
                set_pos(icon, ICON_X * (1.0f - f), y * (1.0f - f), ROW_Z + (CD_Z - ROW_Z) * f);
            } else {
                x = ICON_X;
                yy = y;
                if (off) {
                    leave_xf(e, x, y, &x, &yy);
                }
                set_pos(icon, x, yy, ROW_Z + 0.78f);
            }
            set_scale(icon, sc, sc, sc);
            /* the selected disc spins as in the CD player; the others rest */
            if (sel) {
                icon->rot_tw[1].step = SPIN_Y;
                icon->rot_tw[2].step = SPIN_Z;
            } else {
                icon->rot_tw[0].cur = icon->rot_tw[1].cur = icon->rot_tw[2].cur = 0;
                icon->rot_tw[0].step = icon->rot_tw[1].step = icon->rot_tw[2].step = 0;
            }
            set_hidden(icon, !on);
        }
        if (text) {
            text->text_w = BLIST_TEXT_W;
            text->text_h = BLIST_TEXT_H;
            set_pos(text, TEXT_X, y, ROW_Z + 1.5f);
            int show = on && !off;
            text->flags = show ? (text->flags | BVM_F_TEXT) : (text->flags & ~(uint32_t)BVM_F_TEXT);
        }
        if (num) {
            num->text_w = BLIST_NUM_W;
            num->text_h = BLIST_TEXT_H;
            set_pos(num, NUM_TEXT_X, y, ROW_Z + 1.5f);
            int show = multi && !off;
            num->flags = show ? (num->flags | BVM_F_TEXT) : (num->flags & ~(uint32_t)BVM_F_TEXT);
        }
    }

    for (int i = 0; i < BLIST_BUTTONS; i++) {
        bvm_obj* b = bvm_find(vm, ID_BUTTON(i));
        if (b) {
            set_hidden(b, l->launching);
        }
    }
    bvm_obj* back = bvm_find(vm, 0x1110);
    if (back) {
        set_pos(back, BACK_X, BACK_Y, BACK_Z);
        set_scale(back, BACK_SCALE, BACK_SCALE, BACK_SCALE);
        set_hidden(back, l->launching);
    }
}

void
blist_draw(blist* l, const bscene_sink* sink) {
    static const unsigned passes[2] = {BSCENE_PART_MODEL, BSCENE_PART_TEXT};
    bmenu* m = l->m;
    for (int pass = 0; pass < 2; pass++) {
        m->scene.parts = passes[pass];
        for (int i = 0; i < m->vm.count; i++) {
            const bvm_obj* o = &m->vm.objs[m->vm.order[i]];
            m->scene.fullbright = o->id >= ID_MINI(0) && o->id < ID_MINI(BLIST_MAX_SLOTS);
            m->scene.no_decals = o->id >= ID_BUTTON(0) && o->id < ID_BUTTON(BLIST_BUTTONS);
            /* shorten bars and pills along x without squeezing their ends */
            m->scene.stretch_on = 1;
            if (o->id >= ID_ANCHOR(0) && o->id < ID_ANCHOR(BLIST_MAX_SLOTS)) {
                m->scene.stretch_a = BAR_CAP_L;
                m->scene.stretch_b = BAR_CAP_R;
                m->scene.stretch_f = BAR_MIDDLE_F;
            } else if (o->id >= ID_PILL(0) && o->id < ID_PILL(BLIST_MAX_SLOTS)) {
                m->scene.stretch_a = PILL_CAP_L;
                m->scene.stretch_b = PILL_CAP_R;
                m->scene.stretch_f = PILL_F;
            } else if (o->id >= ID_SPILL(0) && o->id < ID_SPILL(BLIST_MAX_SLOTS)) {
                m->scene.stretch_a = PILL_CAP_L;
                m->scene.stretch_b = PILL_CAP_R;
                m->scene.stretch_f = SPILL_F;
            } else {
                m->scene.stretch_on = 0;
            }
            bscene_draw_object(&m->scene, o, sink);
        }
    }
    m->scene.fullbright = 0;
    m->scene.stretch_on = 0;
    m->scene.no_decals = 0;
    m->scene.parts = BSCENE_PART_ALL;
}
