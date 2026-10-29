/*
 * Host test of ps2_pack / ps2_draw / ps2_ui with a synthetic pack (no PS2 data needed): the pack
 * is built in memory, then the menu is driven through all its screens.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ps2_ui.h"

static int failures;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                  \
        }                                                                \
    } while (0)

/* ---- Synthetic pack ----------------------------------------------------------------------- */

static uint8_t pack_buf[1 << 20] __attribute__((aligned(32)));
static size_t pack_len;
static int entry_count;
static uint32_t data_at;

static void
put32(size_t off, uint32_t v) {
    pack_buf[off] = (uint8_t)v;
    pack_buf[off + 1] = (uint8_t)(v >> 8);
    pack_buf[off + 2] = (uint8_t)(v >> 16);
    pack_buf[off + 3] = (uint8_t)(v >> 24);
}

static void
add_entry(const char* name, uint32_t kind, const void* blob, uint32_t len, uint32_t p0, uint32_t p1, uint32_t p2, uint32_t p3,
          uint32_t p4) {
    size_t e = 16 + 48 * (size_t)entry_count++;
    memcpy(pack_buf + e, name, strlen(name));
    put32(e + 16, kind);
    put32(e + 20, data_at);
    put32(e + 24, len);
    put32(e + 28, p0);
    put32(e + 32, p1);
    put32(e + 36, p2);
    put32(e + 40, p3);
    put32(e + 44, p4);
    memcpy(pack_buf + data_at, blob, len);
    data_at = (data_at + len + 31) & ~31u;
}

static void
add_tex(const char* name, int w, int h) {
    uint8_t* px = calloc((size_t)w * (size_t)h, 2);
    for (int i = 0; i < w * h * 2; i++) {
        px[i] = 0xFF;
    }
    add_entry(name, 1, px, (uint32_t)(w * h * 2), (uint32_t)w, (uint32_t)h, 0, (uint32_t)w, (uint32_t)h);
    free(px);
}

/* a box: 12 triangles, normals pointing out */
static void
add_box(const char* name, float hx, float hy, float hz) {
    static float v[36 * 8];
    int n = 0;
    static const int faces[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
    static const float nrm[6][3] = {{0, 0, -1}, {0, 0, 1}, {0, -1, 0}, {0, 1, 0}, {-1, 0, 0}, {1, 0, 0}};
    for (int f = 0; f < 6; f++) {
        static const int tris[2][3] = {{0, 1, 2}, {0, 2, 3}};
        for (int t = 0; t < 2; t++) {
            for (int k = 0; k < 3; k++) {
                int c = faces[f][tris[t][k]];
                float* o = v + n * 8;
                o[0] = (c & 1) ? hx : -hx;
                o[1] = (c & 2) ? hy : 0.0f;
                o[2] = (c & 4) ? hz : -hz;
                o[3] = nrm[f][0];
                o[4] = nrm[f][1];
                o[5] = nrm[f][2];
                o[6] = (float)(k & 1);
                o[7] = (float)(k >> 1);
                n++;
            }
        }
    }
    add_entry(name, 2, v, sizeof(v), 36, 0, 0, 0, 0);
}

static void
build_pack(void) {
    memset(pack_buf, 0, sizeof(pack_buf));
    memcpy(pack_buf, "PS2P", 4);
    put32(4, 1);
    put32(12, 16);
    data_at = 16 + 48 * 40;
    data_at = (data_at + 31) & ~31u;
    static const char* const tex[] = {"OFOG0", "OFOG1", "OFOG2", "OFOG3", "OFOG4", "OFLAR", "OREF", "CBLUR", "CSMOK", "BNAV1",
                                      "BNAV2", "OCRLE", "BBTTN", "BARRW", "CKABE", "OWAL0", "TBPS2D", "TBPS2M", "TBFNOR",
                                      "TBFSCE", "TBYSYS"};
    for (size_t i = 0; i < sizeof(tex) / sizeof(tex[0]); i++) {
        add_tex(tex[i], 64, 64);
    }
    add_tex("FONT", 256, 512);
    add_box("MBPS2D", 1.7f, 0.3f, 1.7f);
    add_box("MBPS2M", 1.3f, 0.2f, 1.8f);
    add_box("MBFNOR", 1.04f, 2.08f, 1.04f);
    add_box("MBYSYS", 1.3f, 4.0f, 0.2f);
    uint8_t glyphs[96 * 4];
    for (int i = 0; i < 96; i++) {
        glyphs[4 * i] = 4;
        glyphs[4 * i + 1] = 18;
        glyphs[4 * i + 2] = i == 0 ? 9 : 16;
        glyphs[4 * i + 3] = 0;
    }
    add_entry("FONTMETRICS", 3, glyphs, sizeof(glyphs), 0x20, 96, 32, 40, 0);
    put32(8, (uint32_t)entry_count);
    pack_len = data_at;
}

/* ---- Fake host ---------------------------------------------------------------------------- */

static int cursor, count = 12, launched;
static int settings[3];
static int left_settings;

static int h_count(void* u) { (void)u; return count; }
static int h_cursor(void* u) { (void)u; return cursor; }
static void
h_get(void* u, int i, ps2_game_entry* e) {
    (void)u;
    snprintf(e->name, sizeof(e->name), "Game number %d with a rather long title to cut", i);
    snprintf(e->info, sizeof(e->info), "MK-%05d  JUE", i);
    snprintf(e->product, sizeof(e->product), "MK-%05d", i);
    e->is_folder = i == 3;
    e->disc_total = i == 4 ? 2 : 1;
}
static int
h_nav(void* u, int nav) {
    (void)u;
    if (nav == PS2_NAV_NEXT || nav == PS2_NAV_ROW_NEXT) {
        if (cursor < count - 1) cursor += nav == PS2_NAV_NEXT ? 1 : 5;
        if (cursor > count - 1) cursor = count - 1;
        return PS2_NAVR_CHANGED;
    }
    if (nav == PS2_NAV_PREV && cursor > 0) {
        cursor--;
        return PS2_NAVR_CHANGED;
    }
    if (nav == PS2_NAV_ENTER) return PS2_NAVR_LAUNCH;
    if (nav == PS2_NAV_BACK) return PS2_NAVR_EXIT;
    return PS2_NAVR_NONE;
}
static void h_launch(void* u) { (void)u; launched++; }
static int h_anim(void* u) { (void)u; return 1; }
static int h_set_count(void* u) { (void)u; return 3; }
static const char* h_set_label(void* u, int r) { (void)u; (void)r; return "Setting"; }
static const char* h_set_text(void* u, int r) { (void)u; (void)r; return "text"; }
static const char* h_set_value(void* u, int r) { (void)u; return settings[r] ? "On" : "Off"; }
static void h_set_change(void* u, int r, int d) { (void)u; settings[r] = (settings[r] + d + 2) % 2; }
static int h_set_leave(void* u) { (void)u; left_settings++; return 0; }
static int h_card(void* u, int s) { (void)u; return s == 0; }
static const char* h_version(void* u) { (void)u; return "OpenMenu test"; }

static unsigned total_tris;
static int bad_vertex;

static void
tri(void* user, const ps2_vtx v[3], int tex, int blend) {
    (void)user;
    (void)tex;
    (void)blend;
    total_tris++;
    for (int i = 0; i < 3; i++) {
        if (!isfinite(v[i].x) || !isfinite(v[i].y) || !isfinite(v[i].u) || !isfinite(v[i].v) || v[i].z <= 0.0f) {
            bad_vertex++;
        }
    }
}

static void
bind_art(void* user, int slot, const char* product) {
    (void)user;
    CHECK(slot >= 0 && slot < PS2_ART_SLOTS);
    CHECK(product && product[0]);
}

static void
run(ps2_ui* ui, float seconds) {
    for (float t = 0; t < seconds; t += 1.0f / 60.0f) {
        ps2_ui_update(ui, 1.0f / 60.0f);
    }
}

static void
draw_all(ps2_ui* ui) {
    unsigned before = total_tris;
    ps2_ui_draw(ui);
    CHECK(total_tris > before);
}

int
main(void) {
    build_pack();
    static ps2_pack pack;
    CHECK(ps2_pack_open(&pack, pack_buf, pack_len) == PS2_PACK_OK);
    CHECK(ps2_pack_find_tex(&pack, "FONT") >= 0);
    CHECK(ps2_pack_find_tex(&pack, "NOPE") < 0);
    CHECK(ps2_pack_find_model(&pack, "MBFNOR") >= 0);
    CHECK(pack.glyph_count == 96 && pack.cell_h == 40);

    /* damaged packs are refused, not trusted */
    static ps2_pack bad;
    CHECK(ps2_pack_open(&bad, pack_buf, 8) == PS2_PACK_ERR_MAGIC);
    CHECK(ps2_pack_open(&bad, "NOPE0000000000000000", 20) == PS2_PACK_ERR_MAGIC);
    CHECK(ps2_pack_open(&bad, pack_buf, 100) != PS2_PACK_OK);

    ps2_host host;
    memset(&host, 0, sizeof(host));
    host.game_count = h_count;
    host.game_cursor = h_cursor;
    host.game_get = h_get;
    host.game_nav = h_nav;
    host.game_launch = h_launch;
    host.launch_anim = h_anim;
    host.set_count = h_set_count;
    host.set_label = h_set_label;
    host.set_group = h_set_text;
    host.set_help = h_set_text;
    host.set_value = h_set_value;
    host.set_change = h_set_change;
    host.set_leave = h_set_leave;
    host.memcard_present = h_card;
    host.version = h_version;
    ps2_sink sink = {NULL, tri, bind_art};

    static ps2_ui ui;
    CHECK(ps2_ui_init(&ui, &pack, &host, &sink) == 0);

    /* start: fade in, input is ignored until the fade is over */
    ps2_ui_button(&ui, PS2_BTN_A);
    CHECK(ui.screen == PS2_SCREEN_MAIN && !ui.transitioning);
    run(&ui, 0.7f);
    draw_all(&ui);

    /* triangle on the main menu opens the version screen, circle comes back */
    ps2_ui_button(&ui, PS2_BTN_Y);
    run(&ui, 1.2f);
    CHECK(ui.screen == PS2_SCREEN_VERSION);
    draw_all(&ui);
    ps2_ui_button(&ui, PS2_BTN_B);
    run(&ui, 1.2f);
    CHECK(ui.screen == PS2_SCREEN_MAIN);

    /* Browser -> devices -> disc -> games */
    ps2_ui_button(&ui, PS2_BTN_A);
    CHECK(ui.transitioning);
    run(&ui, 1.2f);
    CHECK(ui.screen == PS2_SCREEN_DEVICES);
    draw_all(&ui);
    ps2_ui_button(&ui, PS2_BTN_A); /* the disc is selected first */
    run(&ui, 1.2f);
    CHECK(ui.screen == PS2_SCREEN_GAMES);
    draw_all(&ui);
    ps2_ui_button(&ui, PS2_BTN_RIGHT);
    CHECK(cursor == 1);
    run(&ui, 0.5f);
    draw_all(&ui);
    CHECK(fabsf(ui.grid_top - (float)ui.grid_target) < 0.01f); /* the grid has caught up with the cursor */

    /* start a game: the animation runs, then the host launches */
    ps2_ui_button(&ui, PS2_BTN_A);
    CHECK(ui.launching && launched == 0);
    run(&ui, 0.3f);
    draw_all(&ui);
    CHECK(launched == 0);
    run(&ui, 1.2f);
    CHECK(launched == 1 && !ui.launching);
    run(&ui, 0.1f);
    CHECK(ui.notice_t > 0.0f); /* launch() returned: it failed, the player is told */
    draw_all(&ui);

    /* back out through the devices to a memory card */
    ps2_ui_button(&ui, PS2_BTN_B);
    run(&ui, 1.2f);
    CHECK(ui.screen == PS2_SCREEN_DEVICES);
    ps2_ui_button(&ui, PS2_BTN_LEFT);
    ps2_ui_button(&ui, PS2_BTN_LEFT);
    CHECK(ui.dev_sel == 0);
    ps2_ui_button(&ui, PS2_BTN_A);
    run(&ui, 1.2f);
    CHECK(ui.screen == PS2_SCREEN_CARD);
    draw_all(&ui);
    ps2_ui_button(&ui, PS2_BTN_B);
    run(&ui, 1.2f);
    ps2_ui_button(&ui, PS2_BTN_B);
    run(&ui, 1.2f);
    CHECK(ui.screen == PS2_SCREEN_MAIN);

    /* settings: toggle, open a popup, leave (saves) */
    ps2_ui_button(&ui, PS2_BTN_DOWN);
    CHECK(ui.main_sel == 1);
    ps2_ui_button(&ui, PS2_BTN_A);
    run(&ui, 1.2f);
    CHECK(ui.screen == PS2_SCREEN_SETTINGS);
    draw_all(&ui);
    ps2_ui_button(&ui, PS2_BTN_A); /* A and up/down change the value of the picked setting */
    CHECK(settings[0] == 1);
    ps2_ui_button(&ui, PS2_BTN_UP);
    CHECK(settings[0] == 0);
    ps2_ui_button(&ui, PS2_BTN_RIGHT); /* turn the ring to the next setting */
    CHECK(ui.settings_cursor == 1);
    ps2_ui_button(&ui, PS2_BTN_DOWN);
    CHECK(settings[1] == 1);
    run(&ui, 0.5f);
    draw_all(&ui);
    ps2_ui_button(&ui, PS2_BTN_B);
    run(&ui, 1.2f);
    CHECK(left_settings == 1 && ui.screen == PS2_SCREEN_MAIN);

    /* no games: the list screen must not divide by zero or read a missing entry */
    count = 0;
    cursor = 0;
    ps2_ui_show(&ui, PS2_SCREEN_GAMES);
    run(&ui, 0.1f);
    draw_all(&ui);

    CHECK(bad_vertex == 0);
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("ps2_engine tests passed (%u triangles drawn)\n", total_tris);
    return 0;
}
