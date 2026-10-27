/* bios_page: see bios_page.h. */
#include "bios_page.h"

#define PRIO 0x1000
#define PX_PER_UNIT 11.32f /* 4000 / 353.5: pixels per world unit at the rows' depth */
#define ROW_Z (-353.515625f)
#define DIGIT_SCALE 0.4f /* the digit models are much larger than the icons */

/* Layout in world units (screen centre = 0,0) and pixels, measured on the original Settings screen: the row bars span x 40..619,
 * the value pill (model 33, unscaled) x 256..615 and 45 px high, the icons are centred on x 75, the labels start at x 110 and the
 * values at x 351, the help box (model 34) spans x 188..622, y 340..432 with the BACK marker to its left. Everything keeps its
 * original size: only the rows are closer together (54 px instead of 78) so that five fit the box above the information bar. */
#define ANCHOR_X 1.95f
#define ICON_X (-21.65f)
#define ICON_Z (ROW_Z + 2.5f)
#define PILL_X 9.32f
#define PILL_SCALE 0.859375f
#define TEXT_X 4.58f /* centre of the 512 px text surface: its left edge is 10 px (PAGE_TEXT_PAD) left of where the label starts */
#define BAR_H_PX 53.0f /* height of the row bar model at scale 0.8671875 */
#define BAR_SCALE 0.8671875f
#define BAR_LEFT_UNIT (-30.75003f) /* leftmost x of the row bar model */
#define BAR_WIDTH_UNIT 59.0f
#define BAR_CAP_L (-28.0f) /* the rounded ends of the bar (bios_list.c): the middle between them can be shortened */
#define BAR_CAP_R 27.5f
/* the two boxes, in pixels */
#define BOX_TOP_PX 48.0f
#define BOX_PAD_PX 7.0f
#define ROW_PITCH_PX 54.0f
#define BOX_BOTTOM_PX (BOX_TOP_PX + 2.0f * BOX_PAD_PX + (float)BPAGE_SLOTS * ROW_PITCH_PX)
#define INFO_LEFT_PX 165.0f /* right of the BACK marker (x 90..150), under the rows, as the original's help box */
#define INFO_RIGHT_PX 620.0f
#define INFO_TOP_PX 340.0f
#define INFO_BOTTOM_PX 432.0f
#define INFO_PAD_L_PX 30.0f /* the bar's rounded ends */
#define INFO_PAD_R_PX 24.0f
#define ARROW_SCALE 0.6f
#define ARROW_X_PX 590.0f /* over the pill's right end, on the first / last row */

#define ID_BOX 0x1700
#define ID_INFO 0x1701
#define ID_PILL(s) ((uint16_t)(0x1720 + (s)))
#define ID_ICON(s) ((uint16_t)(0x1740 + (s)))
#define ID_UP 0x1760
#define ID_DOWN 0x1761

static float
bar_left_px(void) {
    return 320.0f + (ANCHOR_X + BAR_LEFT_UNIT * BAR_SCALE) * PX_PER_UNIT;
}

static float
bar_right_px(void) {
    return 320.0f + (ANCHOR_X + (BAR_LEFT_UNIT + BAR_WIDTH_UNIT) * BAR_SCALE) * PX_PER_UNIT;
}

/* y of the row in `slot`, in world units */
static float
row_y(int slot) {
    float centre_px = (BOX_TOP_PX + BOX_BOTTOM_PX) * 0.5f;
    return (240.0f - centre_px) / PX_PER_UNIT + ((float)(BPAGE_SLOTS - 1) * 0.5f - (float)slot) * ROW_PITCH_PX / PX_PER_UNIT;
}

/* BIOS icon k: model 43+k with motion 14+k. Digit d: model 21+d (no motion). */
static void
icon_model(int icon, uint16_t* model, uint16_t* motion, int* has_motion) {
    if (icon >= 10 && icon <= 19) {
        *model = (uint16_t)(21 + icon - 10);
        *motion = 0;
        *has_motion = 0;
    } else {
        int k = icon < 0 || icon > 3 ? 3 : icon;
        *model = (uint16_t)(43 + k);
        *motion = (uint16_t)(14 + k);
        *has_motion = 1;
    }
}

static void
set_pos(bvm_obj* o, float x, float y, float z) {
    o->flags &= ~(uint32_t)BVM_F_ATTACHED; /* absolute: the scripts attached these to the original's anchors */
    o->pos_tw[0].cur = o->pos[0] = x;
    o->pos_tw[1].cur = o->pos[1] = y;
    o->pos_tw[2].cur = o->pos[2] = z;
    o->pos_tw[0].step = o->pos_tw[1].step = o->pos_tw[2].step = 0;
    o->pos_tw[0].frames = o->pos_tw[1].frames = o->pos_tw[2].frames = 0;
}

static void
set_hidden(bvm_obj* o, int hide) {
    o->flags = hide ? (o->flags | BVM_F_HIDE) : (o->flags & ~(uint32_t)BVM_F_HIDE);
}

void
bpage_open(bpage* p, bmenu* m, int count) {
    p->m = m;
    p->count = count < 0 ? 0 : count;
    p->cursor = 0;
    p->top = 0;
    p->back_selected = 0;
    p->anim = 0;
    bvm_kill_all(&m->vm);
    bvm_create(&m->vm, 0x49, ID_BOX, PRIO);  /* the box of the rows: the row bar, made taller in bpage_draw */
    bvm_create(&m->vm, 0x49, ID_INFO, PRIO); /* the box of the information under it */
    for (int s = 0; s < BPAGE_SLOTS; s++) {
        bvm_create(&m->vm, 0x1d, ID_PILL(s), PRIO);
    }
    for (int s = 0; s < BPAGE_SLOTS; s++) {
        bvm_create(&m->vm, 0x4d, ID_ICON(s), PRIO);
    }
    for (int s = 0; s < BPAGE_SLOTS; s++) {
        bvm_create(&m->vm, 0x23, (uint16_t)BPAGE_TEXT_ID(s), PRIO);
    }
    bvm_create(&m->vm, 0x3d, ID_UP, PRIO);   /* the green arrows of the date editor: more rows above / below */
    bvm_create(&m->vm, 0x3e, ID_DOWN, PRIO);
    bvm_create(&m->vm, 6, 0x1110, PRIO);

    /* the BACK marker sits bottom left (settings_screen_update state 0) */
    bvm_obj* back = bvm_find(&m->vm, 0x1110);
    if (back) {
        back->pos_tw[0].cur = -17.57812f;
        back->pos_tw[1].cur = -14.25781f;
    }
}

int
bpage_move(bpage* p, int delta) {
    int next = p->cursor + delta;
    if (next < 0) {
        next = 0;
    }
    if (next > p->count - 1) {
        next = p->count - 1;
    }
    if (next < 0 || next == p->cursor) {
        return 0;
    }
    p->cursor = next;
    if (p->cursor < p->top) {
        p->top = p->cursor;
    }
    if (p->cursor >= p->top + BPAGE_SLOTS) {
        p->top = p->cursor - BPAGE_SLOTS + 1;
    }
    return 1;
}

int
bpage_set_cursor(bpage* p, int row) {
    if (p->back_selected) {
        p->back_selected = 0;
        if (row == p->cursor) {
            return 1;
        }
    }
    return bpage_move(p, row - p->cursor);
}

int
bpage_row_in_slot(const bpage* p, int slot) {
    int row = p->top + slot;
    return slot >= 0 && slot < BPAGE_SLOTS && row < p->count ? row : -1;
}

void
bpage_sync(bpage* p, bpage_row_fn row_fn, void* user) {
    bvm* vm = &p->m->vm;
    p->anim++;
    float box_y = (240.0f - (BOX_TOP_PX + BOX_BOTTOM_PX) * 0.5f) / PX_PER_UNIT;
    float info_y = (240.0f - (INFO_TOP_PX + INFO_BOTTOM_PX) * 0.5f) / PX_PER_UNIT;
    bvm_obj* box = bvm_find(vm, ID_BOX);
    bvm_obj* info = bvm_find(vm, ID_INFO);
    if (box) {
        set_pos(box, ANCHOR_X, box_y, ROW_Z);
        box->scale_tw[0].cur = box->scale_tw[2].cur = BAR_SCALE;
        box->scale_tw[1].cur = BAR_SCALE;
        set_hidden(box, 0);
    }
    if (info) {
        set_pos(info, (INFO_LEFT_PX - 320.0f) / PX_PER_UNIT - BAR_LEFT_UNIT * BAR_SCALE, info_y, ROW_Z);
        info->scale_tw[0].cur = info->scale_tw[2].cur = BAR_SCALE;
        info->scale_tw[1].cur = BAR_SCALE;
        set_hidden(info, 0);
    }
    for (int s = 0; s < BPAGE_SLOTS; s++) {
        int row = bpage_row_in_slot(p, s);
        bvm_obj* pill = bvm_find(vm, ID_PILL(s));
        bvm_obj* icon = bvm_find(vm, ID_ICON(s));
        bvm_obj* text = bvm_find(vm, (uint16_t)BPAGE_TEXT_ID(s));
        int on = row >= 0;
        int sel = on && row == p->cursor && !p->back_selected;
        float y = row_y(s);
        if (pill) {
            set_pos(pill, PILL_X, y, ROW_Z + 0.78f);
            pill->scale_tw[0].cur = pill->scale_tw[2].cur = PILL_SCALE;
            pill->scale_tw[1].cur = PILL_SCALE;
            pill->var[1] = sel;
            set_hidden(pill, !on);
        }
        if (icon) {
            icon->var[0] = sel;
            set_hidden(icon, !on);
            if (on) {
                set_pos(icon, ICON_X, y, ICON_Z);
                if (row_fn) {
                    bpage_row r = {0};
                    int has_motion;
                    row_fn(user, row, &r);
                    icon_model(r.icon, &icon->model, &icon->motion, &has_motion);
                    icon->texlist = icon->model;
                    for (int a = 0; a < 3; a++) {
                        icon->scale_tw[a].cur = has_motion ? 0.9375f : DIGIT_SCALE; /* as the original */
                    }
                    if (!has_motion) {
                        icon->flags &= ~(uint32_t)BVM_F_MOTION;
                    }
                }
            }
        }
        if (text) {
            text->text_w = 512;
            text->text_h = 32;
            set_pos(text, TEXT_X, y, ROW_Z + 1.5f);
            text->flags = on ? (text->flags | BVM_F_TEXT) : (text->flags & ~(uint32_t)BVM_F_TEXT);
        }
    }
    /* the green arrows over the pill's right end of the first / last row, only when there are rows out of sight that way */
    float ax = (ARROW_X_PX - 320.0f) / PX_PER_UNIT;
    bvm_obj* up = bvm_find(vm, ID_UP);
    bvm_obj* down = bvm_find(vm, ID_DOWN);
    if (up) {
        set_pos(up, ax, row_y(0), ROW_Z + 2.0f);
        up->scale_tw[0].cur = up->scale_tw[1].cur = up->scale_tw[2].cur = ARROW_SCALE;
        set_hidden(up, p->top <= 0);
    }
    if (down) {
        set_pos(down, ax, row_y(BPAGE_SLOTS - 1), ROW_Z + 2.0f);
        down->scale_tw[0].cur = down->scale_tw[1].cur = down->scale_tw[2].cur = ARROW_SCALE;
        set_hidden(down, p->top + BPAGE_SLOTS >= p->count);
    }
}

int
bpage_slot_at_px(const bpage* p, float x, float y) {
    if (x < bar_left_px() || x > bar_right_px()) {
        return -1;
    }
    for (int s = 0; s < BPAGE_SLOTS; s++) {
        float cy = 240.0f - row_y(s) * PX_PER_UNIT;
        if (y >= cy - ROW_PITCH_PX * 0.5f && y <= cy + ROW_PITCH_PX * 0.5f && bpage_row_in_slot(p, s) >= 0) {
            return s;
        }
    }
    return -1;
}

void
bpage_info_rect_px(float* x0, float* y0, float* x1, float* y1) {
    *x0 = INFO_LEFT_PX + INFO_PAD_L_PX;
    *x1 = INFO_RIGHT_PX - INFO_PAD_R_PX;
    *y0 = INFO_TOP_PX;
    *y1 = INFO_BOTTOM_PX;
}

/* the extra height of a bar of `height_px` pixels over the model's own, per side, in model units (bios_scene's stretch_yd) */
static float
grow_units(float height_px) {
    return (height_px - BAR_H_PX) * 0.5f / (PX_PER_UNIT * BAR_SCALE);
}

void
bpage_draw(bpage* p, const bscene_sink* sink) {
    static const unsigned passes[2] = {BSCENE_PART_MODEL, BSCENE_PART_TEXT};
    bmenu* m = p->m;
    bmenu_back_style(m, p->back_selected, p->anim, p->pal);
    m->scene.stretch_on = 0;
    for (int pass = 0; pass < 2; pass++) {
        m->scene.parts = passes[pass];
        for (int i = 0; i < m->vm.count; i++) {
            const bvm_obj* o = &m->vm.objs[m->vm.order[i]];
            /* grow the bars along y without squeezing their corners (as the game list's box) */
            m->scene.stretch_yc = m->scene.stretch_yd = m->scene.stretch_yx = 0.0f;
            m->scene.stretch_on = o->id == ID_INFO; /* the information bar is shorter than the rows' */
            if (o->id == ID_INFO) {
                float want = (INFO_RIGHT_PX - INFO_LEFT_PX) / (PX_PER_UNIT * BAR_SCALE); /* model units */
                m->scene.stretch_a = BAR_CAP_L;
                m->scene.stretch_b = BAR_CAP_R;
                m->scene.stretch_f = (BAR_CAP_R - BAR_CAP_L - (BAR_WIDTH_UNIT - want)) / (BAR_CAP_R - BAR_CAP_L);
            }
            if (o->id == ID_BOX || o->id == ID_INFO) {
                m->scene.stretch_yc = 0.72f;
                m->scene.stretch_yx = BAR_LEFT_UNIT;
                m->scene.stretch_yd = grow_units(o->id == ID_BOX ? BOX_BOTTOM_PX - BOX_TOP_PX : INFO_BOTTOM_PX - INFO_TOP_PX);
            }
            bscene_draw_object(&m->scene, o, sink);
        }
    }
    m->scene.stretch_yc = m->scene.stretch_yd = m->scene.stretch_yx = 0.0f;
    m->scene.stretch_on = 0;
    m->scene.parts = BSCENE_PART_ALL;
}

int
bpage_back_at_px(float x, float y) {
    return bmenu_back_hit(-17.57812f, -14.25781f, x, y);
}

/* The cursor table of the BIOS Settings page (0x8C037D70): index 0 is BACK, 1..5 the rows.
 *   row:  up = previous row (the first wraps to the last), down = next row (the last wraps to the first),
 *         left and right = BACK
 *   BACK: up = the row before the last, down / left / right = the last row */
int
bpage_nav(bpage* p, int dir) {
    int n = p->count;
    if (n <= 0) {
        return 0;
    }
    int from = p->back_selected ? -1 : p->cursor;
    int to_row = from, to_back = 0;
    if (p->back_selected) {
        to_row = dir == BPAGE_UP ? (n > 1 ? n - 2 : 0) : n - 1;
    } else if (dir == BPAGE_UP) {
        to_row = from > 0 ? from - 1 : n - 1;
    } else if (dir == BPAGE_DOWN) {
        to_row = from < n - 1 ? from + 1 : 0;
    } else {
        to_back = 1;
    }
    if (to_back) {
        p->back_selected = 1;
        return 1;
    }
    int changed = p->back_selected || to_row != p->cursor;
    p->back_selected = 0;
    p->cursor = to_row;
    if (p->cursor < p->top) {
        p->top = p->cursor;
    }
    if (p->cursor >= p->top + BPAGE_SLOTS) {
        p->top = p->cursor - BPAGE_SLOTS + 1;
    }
    return changed;
}
