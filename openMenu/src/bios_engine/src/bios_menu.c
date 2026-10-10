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
bmenu_select(bmenu* m, int i) {
    if (i < 0 || i >= BMENU_ICONS || i == m->selected) {
        return 0;
    }
    m->selected = i;
    apply_selection(m);
    return 1;
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

/* Each icon is drawn twice (0x200 + i and 0x300 + i): the same model, motion frame, rotation and colour, the second
 * one 0.117 units higher and 0.039 farther. The lighting depends on the normals only, so the second copy is the
 * first one's triangles projected again from the shifted position: no second transform and lighting. */
#define TWIN_MAX 2048
typedef struct {
    bscene_vtx v[3];
    bscene_texref tex;
} twin_tri;
static twin_tri twin_buf[TWIN_MAX];
static int twin_n, twin_overflow;
static const bscene_sink* twin_out;

static void
twin_triangle(void* user, const bscene_vtx v[3], bscene_texref tex) {
    (void)user;
    if (twin_n < TWIN_MAX) {
        memcpy(twin_buf[twin_n].v, v, sizeof(twin_buf[twin_n].v));
        twin_buf[twin_n].tex = tex;
        twin_n++;
    } else {
        twin_overflow = 1;
    }
    twin_out->triangle(twin_out->user, v, tex);
}

static int
twins(const bvm_obj* a, const bvm_obj* b) {
    if (!a->active || !b->active || ((a->flags ^ b->flags) & ~(uint32_t)(BVM_F_ATTACHED | BVM_F_TEXT)) || a->model != b->model ||
        a->texlist != b->texlist || (a->flags & (BVM_F_HIDE | BVM_F_PANEL)) || !(a->flags & BVM_F_MODEL)) {
        return 0;
    }
    if ((a->flags & BVM_F_MOTION) && (a->motion != b->motion || a->motion_tw.cur != b->motion_tw.cur)) {
        return 0;
    }
    if ((a->flags & BVM_F_COLOUR) && memcmp(a->color, b->color, sizeof(a->color))) {
        return 0;
    }
    for (int k = 0; k < 3; k++) {
        if (a->rot[k] != b->rot[k] || a->scale_tw[k].cur != b->scale_tw[k].cur) {
            return 0;
        }
    }
    return 1;
}

/* the captured triangles of `a`, seen from where `b` is */
static void
twin_replay(const bvm_obj* a, const bvm_obj* b, const bscene_sink* sink) {
    const float dx = b->pos[0] - a->pos[0], dy = b->pos[1] - a->pos[1], dz = b->pos[2] - a->pos[2];
    const float half_w = BSCENE_SCREEN_W / 2, half_h = BSCENE_SCREEN_H / 2;
    for (int t = 0; t < twin_n; t++) {
        bscene_vtx v[3];
        int ok = 1;
        for (int k = 0; k < 3; k++) {
            const bscene_vtx* s = &twin_buf[t].v[k];
            v[k] = *s;
            /* back to view space (z = -1 / invw), shift, project again */
            float w = 1.0f / s->invw;
            float x = (s->x - half_w) * w + BSCENE_FOCAL * dx, y = (half_h - s->y) * w + BSCENE_FOCAL * dy;
            float nw = w - dz;
            if (nw < 1.0f) {
                ok = 0;
                break;
            }
            float iw = 1.0f / nw;
            v[k].x = half_w + x * iw;
            v[k].y = half_h - y * iw;
            v[k].invw = iw;
        }
        if (ok) {
            sink->triangle(sink->user, v, twin_buf[t].tex);
        }
    }
}

void
bmenu_draw_objects(bmenu* m, const bscene_sink* sink) {
    static const unsigned passes[2] = {BSCENE_PART_MODEL, BSCENE_PART_TEXT};
    for (int pass = 0; pass < 2; pass++) {
        m->scene.parts = passes[pass];
        /* the BIOS has the PVR sort its translucent polygons per pixel (autosort); without it the models go
         * through the CPU sorter */
        const int cpu_sort = pass == 0 && !m->hw_autosort;
        const bscene_sink* to = cpu_sort ? bscene_sort_begin(sink) : sink;
        uint8_t done[BVM_MAX_OBJECTS] = {0};
        for (int i = 0; i < m->vm.count; i++) {
            const int oi = m->vm.order[i];
            const bvm_obj* o = &m->vm.objs[oi];
            if (done[oi]) {
                continue;
            }
            /* an icon of the first layer: draw it, then its twin of the second layer from the same triangles */
            const bvm_obj* twin = NULL;
            int twin_index = -1;
            /* only for an icon that moves: one at rest comes out of the scene's triangle cache already */
            if (pass == 0 && (o->flags & BVM_F_MOTION) && o->id >= BMENU_ID_ICON(0) && o->id < BMENU_ID_ICON(BMENU_ICONS)) {
                for (int k = 0; k < m->vm.count; k++) {
                    const bvm_obj* c = &m->vm.objs[m->vm.order[k]];
                    if (c->id == o->id + 0x100 && twins(o, c)) {
                        twin = c;
                        twin_index = m->vm.order[k];
                        break;
                    }
                }
            }
            if (!twin) {
                bscene_draw_object(&m->scene, o, to);
                continue;
            }
            bscene_sink cap = {NULL, twin_triangle, to->text};
            twin_out = to;
            twin_n = 0;
            twin_overflow = 0;
            bscene_draw_object(&m->scene, o, &cap);
            if (twin_overflow) {
                bscene_draw_object(&m->scene, twin, to);
            } else {
                twin_replay(o, twin, to);
            }
            done[twin_index] = 1;
        }
        if (cpu_sort) {
            bscene_sort_end();
        }
    }
    m->scene.parts = BSCENE_PART_ALL;
}

void
bmenu_draw(bmenu* m, const bscene_sink* sink) {
    bscene_draw_background(&m->bg, sink);
    bmenu_draw_objects(m, sink);
}

void
bmenu_back_style(bmenu* m, int selected, int anim, int pal) {
    const uint32_t arrow = selected ? (pal ? 0xD02020F0u : 0xD0F02000u) : 0u;
    const uint32_t frame = selected && ((anim / 16) & 1) == 0 ? 0xFFFFFF00u : 0xC0404040u;
    m->scene.ovr[0].model = 0, m->scene.ovr[0].node = 1, m->scene.ovr[0].poly = 0, m->scene.ovr[0].argb = arrow;
    m->scene.ovr[0].lit = 0;
    m->scene.ovr[1].model = 0, m->scene.ovr[1].node = 2, m->scene.ovr[1].poly = 0, m->scene.ovr[1].argb = frame;
    m->scene.ovr[1].lit = 0;
    m->scene.ovr_n = 2;
}

int
bmenu_back_hit(float x, float y, float px, float py) {
    const float px_per_unit = 11.32f; /* 4000 / 353.5: pixels per world unit at the BACK marker's depth */
    float cx = 320.0f + x * px_per_unit, cy = 240.0f - y * px_per_unit;
    return px >= cx - 34.0f && px <= cx + 34.0f && py >= cy - 34.0f && py <= cy + 34.0f;
}
