/* bios_datetime: see bios_datetime.h. */
#include "bios_datetime.h"

#include <stdio.h>

#define PRIO 0x1000
#define PX 11.9f /* pixels per world unit at the panel's depth (4000 / 335.9) */
#define PANEL_Z (-335.9375f)

#define ID_PANEL 0x1500
#define ID_UP 0x1501
#define ID_DOWN 0x1502
#define ID_SELECT 0x1503
#define ID_CANCEL 0x1504

/* Arrow x per field and region, in the original's units (1/256 world unit): from the BIOS tables */
static const int arrow_x[3][5] = {
    {-3000, -2000, -1200, -400, 300},  /* year month day hour minute, year first */
    {-1450, -3250, -2500, -400, 300},  /* month first */
    {-1450, -2500, -3250, -400, 300},  /* day first */
};

int
bdt_days_in_month(int year, int month) {
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return month == 2 && leap ? 29 : days[(month - 1 + 12) % 12];
}

static void
clamp_day(bdt* d) {
    int n = bdt_days_in_month(d->year, d->month);
    if (d->day > n) {
        d->day = n;
    }
}

void
bdt_open(bdt* d, bmenu* m, int order, int year, int month, int day, int hour, int minute) {
    d->m = m;
    d->order = order < 0 || order > 2 ? BDT_ORDER_MDY : order;
    d->year = year < BDT_YEAR_MIN ? BDT_YEAR_MIN : (year > BDT_YEAR_MAX ? BDT_YEAR_MAX : year);
    d->month = month < 1 ? 1 : (month > 12 ? 12 : month);
    d->day = day < 1 ? 1 : day;
    d->hour = hour < 0 ? 0 : (hour > 23 ? 23 : hour);
    d->minute = minute < 0 ? 0 : (minute > 59 ? 59 : minute);
    clamp_day(d);
    d->cursor = BDT_YEAR;
    /* the field the cursor starts on: the original starts on the first field of the written date */
    d->cursor = d->order == BDT_ORDER_YMD ? BDT_YEAR : (d->order == BDT_ORDER_MDY ? BDT_MONTH : BDT_DAY);

    bvm_kill_all(&m->vm);
    bvm_create(&m->vm, 0x3c, ID_PANEL, PRIO);
    bvm_create(&m->vm, 0x3d, ID_UP, PRIO);
    bvm_create(&m->vm, 0x3e, ID_DOWN, PRIO);
    bvm_create(&m->vm, 0x3f, ID_SELECT, PRIO);
    bvm_create(&m->vm, 0x40, ID_CANCEL, PRIO);
}

/* Fields in the order they are written (left to right). */
static int
field_at(const bdt* d, int pos) {
    static const int orders[3][5] = {
        {BDT_YEAR, BDT_MONTH, BDT_DAY, BDT_HOUR, BDT_MINUTE},
        {BDT_MONTH, BDT_DAY, BDT_YEAR, BDT_HOUR, BDT_MINUTE},
        {BDT_DAY, BDT_MONTH, BDT_YEAR, BDT_HOUR, BDT_MINUTE},
    };
    return orders[d->order][pos];
}

static int
position_of(const bdt* d, int field) {
    for (int i = 0; i < 5; i++) {
        if (field_at(d, i) == field) {
            return i;
        }
    }
    return 0;
}

int
bdt_move(bdt* d, int dx) {
    int before = d->cursor;
    if (d->cursor >= BDT_SELECT) {
        if (dx < 0) {
            d->cursor = field_at(d, 4);
        }
    } else {
        int pos = position_of(d, d->cursor) + dx;
        if (pos > 4) {
            d->cursor = BDT_SELECT;
        } else if (pos >= 0) {
            d->cursor = field_at(d, pos);
        }
    }
    return d->cursor != before;
}

static int
wrap(int v, int lo, int hi) {
    if (v > hi) {
        return lo;
    }
    if (v < lo) {
        return hi;
    }
    return v;
}

int
bdt_change(bdt* d, int dy) {
    switch (d->cursor) {
        case BDT_YEAR: d->year = wrap(d->year + dy, BDT_YEAR_MIN, BDT_YEAR_MAX); break;
        case BDT_MONTH: d->month = wrap(d->month + dy, 1, 12); break;
        case BDT_DAY: d->day = wrap(d->day + dy, 1, bdt_days_in_month(d->year, d->month)); break;
        case BDT_HOUR: d->hour = wrap(d->hour + dy, 0, 23); break;
        case BDT_MINUTE: d->minute = wrap(d->minute + dy, 0, 59); break;
        case BDT_SELECT:
        case BDT_CANCEL: {
            int next = dy > 0 ? BDT_SELECT : BDT_CANCEL; /* up: Select above, down: Cancel below */
            int moved = next != d->cursor;
            d->cursor = next;
            return moved;
        }
        default: return 0;
    }
    clamp_day(d);
    return 1;
}

void
bdt_format(const bdt* d, char* out, size_t size) {
    switch (d->order) {
        case BDT_ORDER_YMD: snprintf(out, size, "%04d/%02d/%02d %02d:%02d", d->year, d->month, d->day, d->hour, d->minute); break;
        case BDT_ORDER_DMY: snprintf(out, size, "%02d/%02d/%04d %02d:%02d", d->day, d->month, d->year, d->hour, d->minute); break;
        default: snprintf(out, size, "%02d/%02d/%04d %02d:%02d", d->month, d->day, d->year, d->hour, d->minute); break;
    }
}

float
bdt_field_center_px(const bdt* d, int field) {
    if (field < 0 || field > BDT_MINUTE) {
        return 0.0f;
    }
    return 320.0f + (float)arrow_x[d->order][field] / 256.0f * PX;
}

float
bdt_text_x(const bdt* d) {
    /* centre of each field in characters of the written date, by region */
    static const float centre[3][5] = {
        {2.0f, 6.0f, 9.0f, 12.0f, 15.0f},  /* YYYY/MM/DD HH:MM: year month day hour minute */
        {8.0f, 1.0f, 4.0f, 12.0f, 15.0f},  /* MM/DD/YYYY HH:MM */
        {8.0f, 4.0f, 1.0f, 12.0f, 15.0f},  /* DD/MM/YYYY HH:MM */
    };
    float sum = 0.0f;
    for (int f = 0; f < 5; f++) {
        sum += bdt_field_center_px(d, f) - (float)BDT_CHAR_W * centre[d->order][f];
    }
    return sum / 5.0f;
}

static void
place(bvm_obj* o, float x, float y, float z) {
    if (!o) {
        return;
    }
    o->flags &= ~(uint32_t)BVM_F_ATTACHED; /* the scripts attach these to the panel object */
    o->pos_tw[0].cur = o->pos[0] = x;
    o->pos_tw[1].cur = o->pos[1] = y;
    o->pos_tw[2].cur = o->pos[2] = z;
}

void
bdt_sync(bdt* d) {
    bvm* vm = &d->m->vm;
    int on_field = d->cursor <= BDT_MINUTE;
    float fx = on_field ? (float)arrow_x[d->order][d->cursor] / 256.0f : 99999.0f;
    bvm_obj* up = bvm_find(vm, ID_UP);
    bvm_obj* down = bvm_find(vm, ID_DOWN);
    bvm_obj* sel = bvm_find(vm, ID_SELECT);
    bvm_obj* can = bvm_find(vm, ID_CANCEL);
    if (up) {
        place(up, fx, -2.34375f, PANEL_Z + 1.5625f); /* rotated half a turn: points up */
        up->flags = on_field ? (up->flags & ~(uint32_t)BVM_F_HIDE) : (up->flags | BVM_F_HIDE);
    }
    if (down) {
        place(down, fx, -6.8359375f, PANEL_Z + 1.5625f);
        down->flags = on_field ? (down->flags & ~(uint32_t)BVM_F_HIDE) : (down->flags | BVM_F_HIDE);
    }
    float bx = (BDT_BUTTON_X - 320.0f) / PX;
    if (sel) {
        place(sel, bx, (240.0f - BDT_SELECT_Y) / PX, PANEL_Z + 0.39f);
        sel->var[1] = d->cursor == BDT_SELECT;
    }
    if (can) {
        place(can, bx, (240.0f - BDT_CANCEL_Y) / PX, PANEL_Z + 0.39f);
        can->var[1] = d->cursor == BDT_CANCEL;
    }
    bvm_obj* panel = bvm_find(vm, ID_PANEL);
    if (panel) {
        panel->flags |= BVM_F_HIDE; /* the window is drawn by the caller */
    }
}

void
bdt_draw(bdt* d, const bscene_sink* sink) {
    static const unsigned passes[2] = {BSCENE_PART_MODEL, BSCENE_PART_TEXT};
    bmenu* m = d->m;
    for (int pass = 0; pass < 2; pass++) {
        m->scene.parts = passes[pass];
        for (int i = 0; i < m->vm.count; i++) {
            bscene_draw_object(&m->scene, &m->vm.objs[m->vm.order[i]], sink);
        }
    }
    m->scene.parts = BSCENE_PART_ALL;
}
