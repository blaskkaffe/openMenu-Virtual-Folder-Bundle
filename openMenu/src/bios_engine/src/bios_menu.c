/*
 * bios_menu: the BIOS main menu, see bios_menu.h.
 * Object scripts and ids are those of main_menu_update in the original.
 */
#include "bios_menu.h"

#include <string.h>

#define PRIO_ICON 0x2000
#define PRIO_HEADER 0x1000

/* The 2x2 cursor table of the original (0x8C038724): {up, down, left, right} per icon */
static const uint8_t cursor_table[BMENU_ICONS][4] = {
    {0, 2, 0, 1}, /* Game */
    {1, 3, 0, 1}, /* Files */
    {0, 2, 2, 3}, /* Music */
    {1, 3, 2, 3}, /* Settings */
};

void
bmenu_init(bmenu* m, const bios_rom* rom, const bvm_host* host) {
    memset(m, 0, sizeof(*m));
    m->rom = rom;
    bvm_init(&m->vm, rom, host);
    bscene_init(&m->scene, rom);
    dcbg_init(&m->bg, 0, 0);
}

void
bmenu_free(bmenu* m) {
    bscene_free(&m->scene);
}

static void
apply_selection(bmenu* m) {
    for (int i = 0; i < BMENU_ICONS; i++) {
        bvm_obj* icon = bvm_find(&m->vm, BMENU_ID_ICON(i));
        bvm_obj* caption = bvm_find(&m->vm, BMENU_ID_CAPTION(i));
        int on = (i == m->selected);
        if (icon) {
            icon->var[0] = on;
        }
        if (caption) {
            caption->var[0] = on;
        }
    }
}

void
bmenu_show_main(bmenu* m, int selected) {
    bvm_kill_all(&m->vm);
    m->selected = (selected >= 0 && selected < BMENU_ICONS) ? selected : 0;
    for (int i = 0; i < BMENU_ICONS; i++) {
        bvm_create(&m->vm, 8 + i, (uint16_t)BMENU_ID_ICON(i), PRIO_ICON);
    }
    bvm_create(&m->vm, 0x48, 0x204, PRIO_ICON);
    bvm_create(&m->vm, 1, BMENU_ID_HEADER, PRIO_HEADER);
    for (int i = 0; i < BMENU_ICONS; i++) {
        bvm_create(&m->vm, 0x53 + i, (uint16_t)BMENU_ID_CAPTION(i), PRIO_ICON);
    }
    apply_selection(m);
}

int
bmenu_move(bmenu* m, bmenu_dir dir) {
    int next = cursor_table[m->selected][dir];
    if (next == m->selected) {
        return 0;
    }
    m->selected = next;
    apply_selection(m);
    return 1;
}

void
bmenu_update(bmenu* m) {
    bvm_update(&m->vm);
    dcbg_step(&m->bg);
    m->frames++;
}

void
bmenu_draw_objects(bmenu* m, const bscene_sink* sink) {
    static const unsigned passes[2] = {BSCENE_PART_MODEL, BSCENE_PART_TEXT};
    for (int pass = 0; pass < 2; pass++) {
        m->scene.parts = passes[pass];
        for (int i = 0; i < m->vm.count; i++) {
            const bvm_obj* o = &m->vm.objs[m->vm.order[i]];
            int caption = o->id >= BMENU_ID_CAPTION(0) && o->id < BMENU_ID_CAPTION(BMENU_ICONS);
            int icon = o->id >= BMENU_ID_ICON(0) && o->id < BMENU_ID_ICON(BMENU_ICONS);
            if (caption) {
                continue;
            }
            m->scene.double_alpha = icon;
            bscene_draw_object(&m->scene, o, sink);
        }
    }
    m->scene.double_alpha = 0;
    m->scene.parts = BSCENE_PART_ALL;
}

void
bmenu_draw(bmenu* m, const bscene_sink* sink) {
    bscene_draw_background(&m->bg, sink);
    bmenu_draw_objects(m, sink);
}
