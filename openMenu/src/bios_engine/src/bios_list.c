/* bios_list: see bios_list.h. */
#include "bios_list.h"

#include <math.h>

#define PRIO 0x1000
#define PX_PER_UNIT 11.32f /* 4000 / 353.5: pixels per world unit at the rows' depth */
#define ROW_Z (-353.515625f)

/* Layout in world units (screen centre = 0,0). */
#define Y_SPAN 31.0f        /* distance between the first and the last row */
#define ROW_SX 0.66f        /* the original row bar is about 580 px wide; this makes it 60% of 640 */
#define ROW_SY 0.62f
#define ANCHOR_X (-7.1f)    /* row bar origin */
#define ICON_X (-22.6f)
#define ICON_SCALE 0.14f
#define TEXT_X (-5.9f)      /* centre of the 300 px text surface */
#define PILL_X_OFF (7.42f * ROW_SX)

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
        bvm_create(&m->vm, proto.text_script, (uint16_t)BLIST_TEXT_ID(s), PRIO);
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

float
blist_row_right_px(void) {
    return 320.0f + (ANCHOR_X + 26.8f * ROW_SX) * PX_PER_UNIT;
}

static float
row_y(const blist* l, int s) {
    float pitch = l->slots > 1 ? Y_SPAN / (float)(l->slots - 1) : 0.0f;
    return Y_SPAN / 2.0f - (float)s * pitch;
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

void
blist_sync(blist* l) {
    bvm* vm = &l->m->vm;
    float t = l->launching ? (float)l->launch_frame / (float)LAUNCH_FRAMES : 0.0f;
    if (t > 1.0f) {
        t = 1.0f;
    }
    float e = smooth(t);
    /* the rows leave in a circle around the screen centre */
    float phi = e * 3.6f;
    float k = 1.0f + e * 3.5f;
    float cs = cosf(phi), sn = sinf(phi);

    for (int s = 0; s < l->slots; s++) {
        int row = blist_row_in_slot(l, s);
        int on = row >= 0;
        int sel = on && row == l->cursor;
        bvm_obj* anchor = bvm_find(vm, ID_ANCHOR(s));
        bvm_obj* pill = bvm_find(vm, ID_PILL(s));
        bvm_obj* icon = bvm_find(vm, ID_ICON(s));
        bvm_obj* text = bvm_find(vm, (uint16_t)BLIST_TEXT_ID(s));
        float y = row_y(l, s);

        /* rotate a point of the row layout about the screen centre while launching */
        float ax = ANCHOR_X, ay = y;
        float px = ANCHOR_X + PILL_X_OFF, ix = ICON_X;
        float rot_deg = 0.0f;
        int hide_row = !on;
        if (l->launching && !sel) {
            float x0[3] = {ax, px, ix};
            float out[3];
            float oy[3];
            for (int i = 0; i < 3; i++) {
                out[i] = k * (cs * x0[i] - sn * ay);
                oy[i] = k * (sn * x0[i] + cs * ay);
            }
            ax = out[0];
            px = out[1];
            ix = out[2];
            if (anchor) set_pos(anchor, out[0], oy[0], ROW_Z);
            if (pill) set_pos(pill, out[1], oy[1], ROW_Z + 0.78f);
            if (icon) set_pos(icon, out[2], oy[2], ROW_Z + 0.78f);
            rot_deg = phi;
            if (anchor) anchor->rot_tw[2].cur = (int32_t)(phi * 65536.0f / 6.2831853f);
            if (pill) pill->rot_tw[2].cur = (int32_t)(phi * 65536.0f / 6.2831853f);
            hide_row = hide_row || (k * 20.0f > 80.0f && e > 0.98f);
        } else {
            if (anchor) {
                set_pos(anchor, ax, ay, ROW_Z);
                anchor->rot_tw[2].cur = 0;
            }
            if (pill) {
                set_pos(pill, px, ay, ROW_Z + 0.78f);
                pill->rot_tw[2].cur = 0;
            }
            if (icon && !(l->launching && sel)) {
                set_pos(icon, ix, ay, ROW_Z + 0.78f);
            }
        }
        (void)rot_deg;

        if (anchor) {
            set_scale(anchor, ROW_SX * 0.8671875f, ROW_SY * 0.8671875f, 0.8671875f);
            set_hidden(anchor, hide_row || (l->launching && sel));
        }
        if (pill) {
            set_scale(pill, ROW_SX * 0.859375f, ROW_SY * 0.859375f, 0.859375f);
            pill->var[1] = sel;
            set_hidden(pill, hide_row || (l->launching && sel));
        }
        if (icon) {
            icon->model = BLIST_DISC_MODEL;
            icon->texlist = (uint16_t)(BLIST_TEXLIST_BASE + s);
            icon->flags &= ~(uint32_t)BVM_F_MOTION;
            icon->var[0] = sel;
            if (l->launching && sel) {
                /* to the CD player's place, keeping the spin */
                float f = smooth(t);
                float sc = ICON_SCALE + (CD_SCALE - ICON_SCALE) * f;
                set_pos(icon, ICON_X * (1.0f - f), y * (1.0f - f), ROW_Z + (CD_Z - ROW_Z) * f);
                set_scale(icon, sc, sc, sc);
            } else {
                set_scale(icon, ICON_SCALE, ICON_SCALE, ICON_SCALE);
            }
            /* the selected disc spins as in the CD player; the others rest */
            if (sel) {
                icon->rot_tw[1].step = SPIN_Y;
                icon->rot_tw[2].step = SPIN_Z;
            } else {
                icon->rot_tw[0].cur = icon->rot_tw[1].cur = icon->rot_tw[2].cur = 0;
                icon->rot_tw[0].step = icon->rot_tw[1].step = icon->rot_tw[2].step = 0;
            }
            set_hidden(icon, hide_row && !(l->launching && sel));
        }
        if (text) {
            text->text_w = BLIST_TEXT_W;
            text->text_h = BLIST_TEXT_H;
            set_pos(text, TEXT_X, y, ROW_Z + 1.5f);
            int show = on && !l->launching;
            text->flags = show ? (text->flags | BVM_F_TEXT) : (text->flags & ~(uint32_t)BVM_F_TEXT);
        }
    }
}

void
blist_draw(blist* l, const bscene_sink* sink) {
    static const unsigned passes[2] = {BSCENE_PART_MODEL, BSCENE_PART_TEXT};
    bmenu* m = l->m;
    for (int pass = 0; pass < 2; pass++) {
        m->scene.parts = passes[pass];
        for (int i = 0; i < m->vm.count; i++) {
            bscene_draw_object(&m->scene, &m->vm.objs[m->vm.order[i]], sink);
        }
    }
    m->scene.parts = BSCENE_PART_ALL;
}
