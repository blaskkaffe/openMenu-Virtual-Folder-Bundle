/* bios_page: see bios_page.h. */
#include "bios_page.h"

#include "bios_models.h"

#include <math.h>

#define PRIO 0x1000
#define PX_PER_UNIT 11.32f /* 4000 / 353.5: pixels per world unit at the rows' depth */
#define ROW_Z (-353.515625f)

/* Layout in world units (screen centre = 0,0) and pixels, measured on the original Settings screen: the row bars span x 40..619,
 * the value pill (model 33, unscaled) x 256..615 and 45 px high, the icons are centred on x 75, the labels start at x 110 and the
 * values at x 351, the help box (model 34) spans x 188..622, y 340..432 with the BACK marker to its left. Everything keeps its
 * original size: only the rows are closer together (54 px instead of 78) so that five fit the box above the information bar. */
#define ANCHOR_X 1.95f
#define ICON_X (-21.65f)
#define ICON_Z (ROW_Z + 2.5f)
#define PILL_SCALE 0.859375f
#define PILL_LEFT_UNIT (-17.5f) /* model 33's x extent -17.5 .. 19.4 */
#define PILL_CAP_L (-17.1f)     /* its rounded ends (bios_list.c): the middle between them is shortened */
#define PILL_CAP_R 19.0f
#define PILL_WIDTH_UNIT 36.9f
/* The value pill is the original's, shortened from the left: it starts at x 304, where the longest label (17 characters at about
 * 10.7 px, centred between x 100 and the pill) fits, and keeps the original's right end (x 615). The value starts 10 px inside it. */
#define PILL_LEFT_PX BPAGE_PILL_LEFT_PX
#define PILL_RIGHT_PX 615.0f
#define TEXT_X 0.0f /* the text object only gives the row's height: the app places the label and the value itself (BPAGE_LABEL_CX_PX) */
#define BAR_H_PX 53.0f /* height of the row bar model at scale 0.8671875 */
#define BAR_SCALE 0.8671875f
#define BAR_LEFT_UNIT (-30.75003f) /* leftmost x of the row bar model */
#define BAR_WIDTH_UNIT 59.25f /* x -30.75 .. 28.5 */
#define BAR_CAP_L (-28.0f) /* the rounded ends of the bar (bios_list.c): the middle between them can be shortened */
#define BAR_CAP_R 27.5f
/* the two boxes, in pixels */
#define BOX_TOP_PX 48.0f
#define BOX_PAD_PX 7.0f
#define ROW_PITCH_PX 54.0f
#define BOX_BOTTOM_PX (BOX_TOP_PX + 2.0f * BOX_PAD_PX + (float)BPAGE_SLOTS * ROW_PITCH_PX)
/* the original's help box (model 42): x -15.76 .. 28.5 on the fourth row's anchor, down to y -12.31, corner radius 1.0, so the same
 * distance to the BACK marker (x 90..150, y 373..433) as the original */
#define INFO_LEFT_PX 187.4f
#define INFO_RIGHT_PX 621.8f
#define INFO_TOP_PX 340.0f
#define INFO_BOTTOM_PX 433.8f
#define INFO_TEXT_TOP_PAD_PX 9.0f /* the original's help text starts this far below the top of its pill */
#define INFO_PAD_R_PX 14.0f
#define ARROW_SCALE 0.5f
#define ARROW_X_PX 631.0f /* in the margin right of the box (which ends at x 621.8), not over it */
#define ARROW_INSET_PX 12.0f /* the arrows' centres from the top / bottom of the box */

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

/* The model of an icon, its motion (0: none) and the scale that makes it as big as the BIOS icons (about 40 px): BIOS icon k is model 43+k with
 * motion 14+k at 0.9375, a digit d model 21+d. The other models differ in size, so each has its own scale. */
static void
icon_model(int icon, uint16_t* model, uint16_t* motion, float* scale) {
    *motion = 0;
    if (icon >= 10 && icon <= 19) {
        *model = (uint16_t)(21 + icon - 10);
        *scale = 0.4f;
        return;
    }
    switch (icon) {
        case BPAGE_ICON_VMU: *model = 1; *motion = 1; *scale = 0.34f; return; /* the main menu's VMU, with its LCD screen */
        case BPAGE_ICON_DISC: *model = 61; *scale = 0.14f; return;
        case BPAGE_ICON_PAD: *model = 17; *scale = 0.55f; return;
        case BPAGE_ICON_GLOBE: *model = BMODEL_GLOBE; *scale = 0.4f; return;
        case BPAGE_ICON_PHONE: *model = BMODEL_PHONE; *scale = 0.42f; return;
        case BPAGE_ICON_CASE: *model = BMODEL_CASE_WHITE; *scale = 0.38f; return;
        default: break;
    }
    int k = icon < 0 || icon > 3 ? 3 : icon;
    *model = (uint16_t)(43 + k);
    *motion = (uint16_t)(14 + k);
    *scale = 0.9375f;
}

/* Models whose origin is not their centre: how far down (world units, at scale 1) the picture has to move to sit on the row's middle. */
static float
icon_dy(int icon) {
    return icon == BPAGE_ICON_VMU ? -7.5f : 0.0f;
}

/* Frames each slot's row has been selected (0: not selected): the icons animate only while their row is selected, as in the BIOS. */
static int icon_age[BPAGE_SLOTS];

/* The models without a motion of their own move by turning a little while selected, in the manner of the BIOS icons (a short loop that
 * starts when the row is selected): returns the angle in degrees about *axis (0 x, 1 y, 2 z). */
static float
icon_turn(int icon, int age, int* axis) {
    const float two_pi = 6.2831853f;
    *axis = 1;
    switch (icon) {
        case BPAGE_ICON_GLOBE: return (float)(age % 180) * 2.0f; /* the earth turns once in 3 s */
        case BPAGE_ICON_PHONE: /* rings: a quick shake in bursts */
            *axis = 2;
            return (age % 60) < 36 ? 9.0f * sinf((float)age * 0.9f) : 0.0f;
        case BPAGE_ICON_CASE: return 35.0f * sinf((float)age * two_pi / 120.0f);
        default: return 25.0f * sinf((float)age * two_pi / 100.0f); /* the controller and the rest: a slow sway */
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
            set_pos(pill, (PILL_LEFT_PX - 320.0f) / PX_PER_UNIT - PILL_LEFT_UNIT * PILL_SCALE, y, ROW_Z + 0.78f);
            pill->scale_tw[0].cur = pill->scale_tw[2].cur = PILL_SCALE;
            pill->scale_tw[1].cur = PILL_SCALE;
            pill->var[1] = sel;
            set_hidden(pill, !on);
        }
        if (icon) {
            icon->var[0] = sel;
            set_hidden(icon, !on);
            if (!on) {
                icon_age[s] = 0;
            }
            if (on) {
                set_pos(icon, ICON_X, y, ICON_Z);
                if (row_fn) {
                    bpage_row r = {0};
                    float scale;
                    row_fn(user, row, &r);
                    icon_model(r.icon, &icon->model, &icon->motion, &scale);
                    set_pos(icon, ICON_X, y + icon_dy(r.icon) * scale, ICON_Z);
                    icon->texlist = icon->model;
                    for (int a = 0; a < 3; a++) {
                        icon->scale_tw[a].cur = scale; /* the BIOS icons at the original's size */
                    }
                    if (!icon->motion) {
                        icon->flags &= ~(uint32_t)BVM_F_MOTION;
                    } else {
                        icon->flags |= BVM_F_MOTION; /* a row that showed a model without motion before */
                    }
                    /* only the selected row's icon animates */
                    icon_age[s] = sel ? icon_age[s] + 1 : 0;
                    if (icon->motion) {
                        if (sel) {
                            if (icon_age[s] == 1) {
                                icon->motion_tw.cur = 0.5f; /* starts from the beginning each time */
                            }
                            icon->motion_tw.step = 0.5f;
                        } else {
                            icon->motion_tw.cur = 0.0f;
                            icon->motion_tw.step = 0.0f;
                        }
                    } else {
                        int axis;
                        float deg = sel ? icon_turn(r.icon, icon_age[s], &axis) : (axis = 1, 0.0f);
                        for (int a = 0; a < 3; a++) {
                            icon->rot_tw[a].step = 0;
                            icon->rot_tw[a].frames = 0;
                            icon->rot_tw[a].cur = a == axis ? (int32_t)(deg * 65536.0f / 360.0f) : 0;
                        }
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
    /* the green arrows at the top and bottom of the box, in the margin to its right, only when there are rows out of sight that way */
    float ax = (ARROW_X_PX - 320.0f) / PX_PER_UNIT;
    bvm_obj* up = bvm_find(vm, ID_UP);
    bvm_obj* down = bvm_find(vm, ID_DOWN);
    if (up) {
        set_pos(up, ax, (240.0f - (BOX_TOP_PX + ARROW_INSET_PX)) / PX_PER_UNIT, ROW_Z + 2.0f);
        up->scale_tw[0].cur = up->scale_tw[1].cur = up->scale_tw[2].cur = ARROW_SCALE;
        set_hidden(up, p->top <= 0);
    }
    if (down) {
        set_pos(down, ax, (240.0f - (BOX_BOTTOM_PX - ARROW_INSET_PX)) / PX_PER_UNIT, ROW_Z + 2.0f);
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
    *x0 = BPAGE_VALUE_X_PX;
    *x1 = INFO_RIGHT_PX - INFO_PAD_R_PX;
    *y0 = INFO_TOP_PX + INFO_TEXT_TOP_PAD_PX;
    *y1 = INFO_BOTTOM_PX;
}

int
bpage_scroll_thumb_px(const bpage* p, float* x, float* y, float* w, float* h) {
    if (p->count <= BPAGE_SLOTS) {
        return 0;
    }
    /* between the arrows, clear of their size */
    float arrow = 2.0f * ARROW_SCALE * PX_PER_UNIT;
    float top = BOX_TOP_PX + ARROW_INSET_PX + arrow * 0.5f + 4.0f;
    float bottom = BOX_BOTTOM_PX - ARROW_INSET_PX - arrow * 0.5f - 4.0f;
    float len = bottom - top;
    float th = len * (float)BPAGE_SLOTS / (float)p->count;
    th = th < 14.0f ? 14.0f : (th > len * 0.5f ? len * 0.5f : th);
    float at = p->count > 1 ? (float)p->cursor / (float)(p->count - 1) : 0.0f;
    *w = arrow * 0.5f; /* the arrow's width halfway up its height */
    *x = ARROW_X_PX - *w * 0.5f;
    *h = th;
    *y = top + (len - th) * at;
    return 1;
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
            m->scene.stretch_on = o->id == ID_INFO || (o->id >= ID_PILL(0) && o->id < ID_PILL(BPAGE_SLOTS)); /* both are shorter than the original's */
            m->scene.small_left = o->id == ID_INFO; /* the information box has the help box's small corners all round */
            if (m->scene.stretch_on && o->id != ID_INFO) {
                float want = (PILL_RIGHT_PX - PILL_LEFT_PX) / (PX_PER_UNIT * PILL_SCALE);
                m->scene.stretch_a = PILL_CAP_L;
                m->scene.stretch_b = PILL_CAP_R;
                m->scene.stretch_f = (PILL_CAP_R - PILL_CAP_L - (PILL_WIDTH_UNIT - want)) / (PILL_CAP_R - PILL_CAP_L);
            }
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
    m->scene.small_left = 0;
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
