/* bios_files: see bios_files.h. Object scripts and positions are those of vmu_card_select_grid. */
#include "bios_files.h"

#define PRIO 0x1000
#define PX 11.4f /* pixels per world unit at the cards' depth (4000 / 351.6) */

#define ID_FRAME 0x1000
#define ID_PLATE 0x1006
#define ID_BACK 0x1110
#define ID_CARD(i) ((uint16_t)(0x1100 + (i)))

/* x of each card column (ports A-D) and y of each row (sockets 1, 2), from the BIOS tables */
static const float col_x[4] = {-8.7890625f, 0.0f, 8.7890625f, 17.3828125f};
static const float row_y[2] = {-2.34375f, -13.671875f};

void
bfiles_open(bfiles* f, bmenu* m, int cursor) {
    f->m = m;
    f->back_selected = 0;
    f->anim = 0;
    f->cursor = cursor < 0 || cursor >= BFILES_SLOTS ? 0 : cursor;
    for (int i = 0; i < BFILES_SLOTS; i++) {
        f->present[i] = 0;
    }
    bvm_kill_all(&m->vm);
    bvm_create(&m->vm, 0xc, ID_FRAME, PRIO);   /* the bar across the top, with the title line */
    bvm_create(&m->vm, 0xe, 0x1002, PRIO);     /* the four controllers A to D */
    bvm_create(&m->vm, 0xf, 0x1003, PRIO);
    bvm_create(&m->vm, 0x10, 0x1004, PRIO);
    bvm_create(&m->vm, 0x11, 0x1005, PRIO);
    bvm_create(&m->vm, 0x15, ID_PLATE, PRIO);  /* the memory card plate with the free blocks */
    bvm_obj* back = bvm_create(&m->vm, 6, ID_BACK, PRIO);
    for (int i = 0; i < BFILES_SLOTS; i++) {
        bvm_obj* c = bvm_create(&m->vm, 0x12, ID_CARD(i), PRIO);
        if (c) {
            c->pos_tw[0].cur = col_x[i / 2];
            c->pos_tw[1].cur = row_y[i % 2];
            c->model = (uint16_t)(62 + i);
            c->texlist = c->model;
            c->motion = 13;
        }
    }
    if (back) {
        back->pos_tw[0].cur = -21.09375f;
        back->pos_tw[1].cur = -13.67188f;
    }
}

int
bfiles_set_cursor(bfiles* f, int slot) {
    if (f->back_selected && slot >= 0 && slot < BFILES_SLOTS) {
        f->back_selected = 0;
        f->cursor = slot;
        return 1;
    }
    if (slot < 0 || slot >= BFILES_SLOTS || slot == f->cursor) {
        return 0;
    }
    f->cursor = slot;
    return 1;
}

/* The cursor table of the BIOS card picker (0x8C03884C): slot = port * 2 + socket - 1, index 8 is BACK.
 * Up and down swap the two sockets of a port; left and right keep the socket and step between ports, past
 * the first or last port the cursor goes to BACK. Order: up, down, left, right. */
static const signed char grid_table[9][4] = {
    {1, 1, 8, 2}, {0, 0, 8, 3}, {3, 3, 0, 4}, {2, 2, 1, 5}, {5, 5, 2, 6},
    {4, 4, 3, 7}, {7, 7, 4, 8}, {6, 6, 5, 8}, {0, 1, 7, 1},
};

int
bfiles_nav(bfiles* f, int dir) {
    int from = f->back_selected ? 8 : f->cursor;
    int to = grid_table[from][dir];
    if (to == from) {
        return 0;
    }
    f->back_selected = to == 8;
    if (to != 8) {
        f->cursor = to;
    }
    return 1;
}

int
bfiles_move(bfiles* f, int dx, int dy) {
    if (dx || dy) {
        return bfiles_nav(f, dx < 0 ? BFILES_LEFT : (dx > 0 ? BFILES_RIGHT : (dy < 0 ? BFILES_UP : BFILES_DOWN)));
    }
    return 0;
}

void
bfiles_sync(bfiles* f) {
    bvm* vm = &f->m->vm;
    f->anim++;
    for (int i = 0; i < BFILES_SLOTS; i++) {
        bvm_obj* c = bvm_find(vm, ID_CARD(i));
        if (!c) {
            continue;
        }
        c->flags |= BVM_F_COLOUR;
        float pulse = (float)((f->m->frames / 4) % 8 < 4 ? 0.0f : 0.12f); /* the selected card flashes */
        if (i == f->cursor && !f->back_selected) {
            c->color[0] = 0.0f;
            c->color[1] = c->color[2] = 0.30f + pulse;
            c->color[3] = -0.10f;
        } else if (f->present[i]) {
            c->color[0] = 0.0f;
            c->color[1] = c->color[2] = c->color[3] = 0.0f;
        } else {
            c->color[0] = -0.55f; /* no card in this socket: faded */
            c->color[1] = c->color[2] = c->color[3] = -0.15f;
        }
    }
}

void
bfiles_draw(bfiles* f, const bscene_sink* sink) {
    static const unsigned passes[2] = {BSCENE_PART_MODEL, BSCENE_PART_TEXT};
    bmenu* m = f->m;
    bmenu_back_style(m, f->back_selected, f->anim, f->pal);
    for (int pass = 0; pass < 2; pass++) {
        m->scene.parts = passes[pass];
        for (int i = 0; i < m->vm.count; i++) {
            bscene_draw_object(&m->scene, &m->vm.objs[m->vm.order[i]], sink);
        }
    }
    m->scene.parts = BSCENE_PART_ALL;
}

void
bfiles_card_px(int slot, float* x, float* y) {
    slot = slot < 0 ? 0 : (slot >= BFILES_SLOTS ? BFILES_SLOTS - 1 : slot);
    *x = 320.0f + col_x[slot / 2] * PX;
    *y = 240.0f - (row_y[slot % 2] - 3.0f) * PX; /* the card model's centre is a little below its position */
}

void
bfiles_title_px(float* x, float* y) {
    *x = 320.0f;
    *y = 62.0f;
}

void
bfiles_plate_px(float* x, float* y) {
    *x = 117.0f;
    *y = 164.0f;
}

int
bfiles_slot_at_px(float x, float y) {
    for (int i = 0; i < BFILES_SLOTS; i++) {
        float cx, cy;
        bfiles_card_px(i, &cx, &cy);
        if (x >= cx - 40.0f && x <= cx + 40.0f && y >= cy - 55.0f && y <= cy + 55.0f) {
            return i;
        }
    }
    return -1;
}

int
bfiles_back_at_px(float x, float y) {
    return bmenu_back_hit(-21.09375f, -13.67188f, x, y);
}
