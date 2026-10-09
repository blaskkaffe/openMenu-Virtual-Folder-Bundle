/* bios_page: see bios_page.h. */
#include "bios_page.h"

#define PRIO 0x1000
#define DIGIT_SCALE 0.4f /* the digit models are much larger than the icons */

/* Per slot: anchor (position holder and row bar), highlight pill, icon, text line. */
static const struct {
    int anchor_script, pill_script, icon_script, text_script;
    uint16_t anchor_id, pill_id, icon_id, text_id;
} slots[BPAGE_SLOTS] = {
    {0x49, 0x1d, 0x4d, 0x23, 0x1600, 0x1401, 0x1604, 0x1407},
    {0x4a, 0x1e, 0x4e, 0x24, 0x1601, 0x1402, 0x1605, 0x1408},
    {0x4b, 0x1f, 0x4f, 0x25, 0x1602, 0x1403, 0x1606, 0x1409},
    {0x4c, 0x21, 0x50, 0x26, 0x1603, 0x1405, 0x1607, 0x140A},
};

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

void
bpage_open(bpage* p, bmenu* m, int count) {
    p->m = m;
    p->count = count < 0 ? 0 : count;
    p->cursor = 0;
    p->top = 0;
    bvm_kill_all(&m->vm);
    bvm_create(&m->vm, 6, 0x1110, PRIO);
    for (int i = 0; i < BPAGE_SLOTS; i++) {
        bvm_create(&m->vm, slots[i].anchor_script, slots[i].anchor_id, PRIO);
    }
    for (int i = 0; i < BPAGE_SLOTS; i++) {
        bvm_create(&m->vm, slots[i].pill_script, slots[i].pill_id, PRIO);
    }
    bvm_create(&m->vm, 0x22, 0x1406, PRIO); /* the help box */
    for (int i = 0; i < BPAGE_SLOTS; i++) {
        bvm_create(&m->vm, slots[i].icon_script, slots[i].icon_id, PRIO);
    }
    for (int i = 0; i < BPAGE_SLOTS; i++) {
        bvm_create(&m->vm, slots[i].text_script, slots[i].text_id, PRIO);
    }
    bvm_create(&m->vm, 0x27, BPAGE_HELP_ID, PRIO);

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
bpage_row_in_slot(const bpage* p, int slot) {
    int row = p->top + slot;
    return slot >= 0 && slot < BPAGE_SLOTS && row < p->count ? row : -1;
}

void
bpage_sync(bpage* p, bpage_row_fn row_fn, void* user) {
    bvm* vm = &p->m->vm;
    for (int s = 0; s < BPAGE_SLOTS; s++) {
        int row = bpage_row_in_slot(p, s);
        bvm_obj* anchor = bvm_find(vm, slots[s].anchor_id);
        bvm_obj* pill = bvm_find(vm, slots[s].pill_id);
        bvm_obj* icon = bvm_find(vm, slots[s].icon_id);
        bvm_obj* text = bvm_find(vm, slots[s].text_id);
        int on = row >= 0;
        int sel = on && row == p->cursor;
        if (anchor) {
            anchor->flags = on ? (anchor->flags & ~(uint32_t)BVM_F_HIDE) : (anchor->flags | BVM_F_HIDE);
        }
        if (pill) {
            pill->var[1] = sel;
            pill->flags = on ? (pill->flags & ~(uint32_t)BVM_F_HIDE) : (pill->flags | BVM_F_HIDE);
        }
        if (icon) {
            icon->var[0] = sel;
            icon->flags = on ? (icon->flags & ~(uint32_t)BVM_F_HIDE) : (icon->flags | BVM_F_HIDE);
            if (on && row_fn) {
                bpage_row r = {0};
                int has_motion;
                row_fn(user, row, &r);
                icon_model(r.icon, &icon->model, &icon->motion, &has_motion);
                icon->texlist = icon->model;
                for (int a = 0; a < 3; a++) {
                    icon->scale_tw[a].cur = has_motion ? 0.9375f : DIGIT_SCALE;
                }
                if (!has_motion) {
                    icon->flags &= ~(uint32_t)BVM_F_MOTION;
                }
            }
        }
        if (text) {
            text->text_w = 512;
            text->text_h = 32;
            text->flags = on ? (text->flags | BVM_F_TEXT) : (text->flags & ~(uint32_t)BVM_F_TEXT);
        }
    }
    bvm_obj* help = bvm_find(vm, BPAGE_HELP_ID);
    if (help) {
        help->text_w = 512;
        help->text_h = 64;
    }
}

void
bpage_draw(bpage* p, const bscene_sink* sink) {
    static const unsigned passes[2] = {BSCENE_PART_MODEL, BSCENE_PART_TEXT};
    bmenu* m = p->m;
    for (int pass = 0; pass < 2; pass++) {
        m->scene.parts = passes[pass];
        for (int i = 0; i < m->vm.count; i++) {
            bscene_draw_object(&m->scene, &m->vm.objs[m->vm.order[i]], sink);
        }
    }
    m->scene.parts = BSCENE_PART_ALL;
}
