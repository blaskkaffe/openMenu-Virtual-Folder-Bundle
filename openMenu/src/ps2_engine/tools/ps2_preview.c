/*
 * ps2_preview: render a screen of the PS2-style menu to a PPM image on the PC, with the same
 * engine code as the console build and a software rasteriser instead of the PVR.
 *
 *   ps2_preview PS2.PAK out.ppm [screen] [seconds] [selection] [popup]
 *
 * screen: main, devices, games, card, settings (default main). seconds: how long the menu has
 * been running (animation phase). selection: the highlighted item (game number, settings row,
 * device). popup: 1 opens the settings choice popup / the recently played list.
 * The data shown is made up (30 games, a few settings).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ps2_ui.h"

#define W 640
#define H 480

static float fb[H][W][3];
static const ps2_pack* g_pack;

/* ---- Fake host data ---------------------------------------------------------------------- */

static int g_cursor, g_count = 62;
static int g_set_values[8];

static const char* const game_names[] = {"Sonic Adventure", "Shenmue", "Crazy Taxi 2", "Skies of Arcadia Disc 1", "Jet Set Radio",
                                         "Soulcalibur", "Power Stone 2", "Resident Evil - Code: Veronica", "Rez", "Space Channel 5",
                                         "Grandia II", "Marvel vs. Capcom 2", "Ikaruga", "Chu Chu Rocket", "Virtua Tennis"};

static int
h_count(void* u) {
    (void)u;
    return g_count;
}
static int
h_cursor(void* u) {
    (void)u;
    return g_cursor;
}
static void
h_get(void* u, int i, ps2_game_entry* e) {
    (void)u;
    snprintf(e->name, sizeof(e->name), "%s", game_names[i % 15]);
    snprintf(e->info, sizeof(e->info), "MK-%05d   NTSC-U   1 player", 51000 + i);
    snprintf(e->product, sizeof(e->product), "MK-%05d", 51000 + i);
    e->is_folder = i % 11 == 10;
    e->disc_total = 1;
    if (i % 7 == 3) {
        e->disc_total = 2;
    }
}
static int
h_nav(void* u, int nav) {
    (void)u;
    if (nav == PS2_NAV_NEXT && g_cursor < g_count - 1) g_cursor++;
    if (nav == PS2_NAV_PREV && g_cursor > 0) g_cursor--;
    return PS2_NAVR_CHANGED;
}
static void
h_folder(void* u, char* out, size_t n) {
    (void)u;
    snprintf(out, n, "%s", "");
}
static int
h_recent_count(void* u) {
    (void)u;
    return 5;
}
static const char*
h_recent_name(void* u, int i) {
    (void)u;
    return game_names[i];
}

static const char* const set_labels[] = {"Game order", "Multi-disc games", "Disc sets from", "Recently played", "Remember last game",
                                         "Boot animation", "Launch animation", "Exit to BIOS"};
static const char* const set_groups[] = {"Game list", "Game list", "Game list", "Game list", "Game list", "Starting games", "Starting games", "Starting games"};
static const char* const set_help[] = {"Order of the games", "List every disc or one entry per set", "Where the other discs are looked for",
                                       "Games in the popup opened with X", "Start browsing where you left off",
                                       "What the BIOS shows before a game", "Discs fly out before a game starts",
                                       "How non-game discs return to the BIOS"};
static const char* const set_choices[] = {"Full", "License only", "Animation only", "Fast"};

static int h_set_count(void* u) { (void)u; return 8; }
static const char* h_set_label(void* u, int r) { (void)u; return set_labels[r]; }
static const char* h_set_group(void* u, int r) { (void)u; return set_groups[r]; }
static const char* h_set_help(void* u, int r) { (void)u; return set_help[r]; }
static const char* h_set_value(void* u, int r) { (void)u; return set_choices[g_set_values[r] & 3]; }
static void h_set_change(void* u, int r, int d) { (void)u; g_set_values[r] += d; }
static const char* h_version(void* u) { (void)u; return "OpenMenu 1.0"; }
static int h_memcard(void* u, int s) { (void)u; return s == 0; }
static void h_clock(void* u, char* out, size_t n) { (void)u; snprintf(out, n, "%s", "2026/10/29 14:25:56"); }

/* ---- Rasteriser ---------------------------------------------------------------------------- */

typedef struct {
    uint32_t* px;
    int w, h;
} bitmap;

static bitmap bmps[PS2_PACK_MAX_TEX];
static bitmap art_bmp[PS2_ART_SLOTS]; /* stand-ins for game pictures */

static void
decode_textures(void) {
    for (int i = 0; i < g_pack->ntex; i++) {
        const ps2_tex_info* t = &g_pack->tex[i];
        bmps[i].w = t->w;
        bmps[i].h = t->h;
        bmps[i].px = malloc(sizeof(uint32_t) * (size_t)t->w * (size_t)t->h);
        for (int k = 0; k < t->w * t->h; k++) {
            uint16_t v = (uint16_t)(t->pixels[2 * k] | (t->pixels[2 * k + 1] << 8));
            uint32_t a, r, g, b;
            if (t->fmt == PS2_FMT_RGB565) {
                a = 255;
                r = ((v >> 11) & 31) * 255 / 31;
                g = ((v >> 5) & 63) * 255 / 63;
                b = (v & 31) * 255 / 31;
            } else if (t->fmt == PS2_FMT_ARGB1555) {
                a = (v >> 15) ? 255 : 0;
                r = ((v >> 10) & 31) * 255 / 31;
                g = ((v >> 5) & 31) * 255 / 31;
                b = (v & 31) * 255 / 31;
            } else {
                a = ((v >> 12) & 15) * 17;
                r = ((v >> 8) & 15) * 17;
                g = ((v >> 4) & 15) * 17;
                b = (v & 15) * 17;
            }
            bmps[i].px[k] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
}

static void
tri(void* user, const ps2_vtx v[3], int tex, int blend) {
    (void)user;
    const bitmap* bm = tex >= 0 && tex < g_pack->ntex ? &bmps[tex] : NULL;
    if (tex >= PS2_TEX_ART && tex < PS2_TEX_ART + PS2_ART_SLOTS && art_bmp[tex - PS2_TEX_ART].px) {
        bm = &art_bmp[tex - PS2_TEX_ART];
    }
    float minx = fminf(v[0].x, fminf(v[1].x, v[2].x)), maxx = fmaxf(v[0].x, fmaxf(v[1].x, v[2].x));
    float miny = fminf(v[0].y, fminf(v[1].y, v[2].y)), maxy = fmaxf(v[0].y, fmaxf(v[1].y, v[2].y));
    int x0 = (int)fmaxf(minx, 0), x1 = (int)fminf(maxx + 1, W - 1), y0 = (int)fmaxf(miny, 0), y1 = (int)fminf(maxy + 1, H - 1);
    float den = (v[1].y - v[2].y) * (v[0].x - v[2].x) + (v[2].x - v[1].x) * (v[0].y - v[2].y);
    if (fabsf(den) < 1e-9f) {
        return;
    }
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float w0 = ((v[1].y - v[2].y) * (px - v[2].x) + (v[2].x - v[1].x) * (py - v[2].y)) / den;
            float w1 = ((v[2].y - v[0].y) * (px - v[2].x) + (v[0].x - v[2].x) * (py - v[2].y)) / den;
            float w2 = 1.0f - w0 - w1;
            if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) {
                continue;
            }
            float col[4];
            for (int c = 0; c < 4; c++) {
                int sh = 24 - 8 * c;
                col[c] = (w0 * (float)((v[0].argb >> sh) & 255) + w1 * (float)((v[1].argb >> sh) & 255) +
                          w2 * (float)((v[2].argb >> sh) & 255)) / 255.0f;
            }
            if (bm) {
                float u = w0 * v[0].u + w1 * v[1].u + w2 * v[2].u;
                float t = w0 * v[0].v + w1 * v[1].v + w2 * v[2].v;
                /* bilinear */
                float fx = u * bm->w - 0.5f, fy = t * bm->h - 0.5f;
                int ix = (int)floorf(fx), iy = (int)floorf(fy);
                float ax = fx - ix, ay = fy - iy;
                float s[4] = {0, 0, 0, 0};
                for (int k = 0; k < 4; k++) {
                    int sx = ix + (k & 1), sy = iy + (k >> 1);
                    float wgt = ((k & 1) ? ax : 1 - ax) * ((k >> 1) ? ay : 1 - ay);
                    sx = sx < 0 ? 0 : (sx >= bm->w ? bm->w - 1 : sx);
                    sy = sy < 0 ? 0 : (sy >= bm->h ? bm->h - 1 : sy);
                    uint32_t p = bm->px[sy * bm->w + sx];
                    s[0] += wgt * (float)(p >> 24) / 255.0f;
                    s[1] += wgt * (float)((p >> 16) & 255) / 255.0f;
                    s[2] += wgt * (float)((p >> 8) & 255) / 255.0f;
                    s[3] += wgt * (float)(p & 255) / 255.0f;
                }
                for (int c = 0; c < 4; c++) col[c] *= s[c];
            }
            for (int c = 0; c < 3; c++) {
                if (blend == PS2_BLEND_ADD) {
                    fb[y][x][c] += col[c + 1] * col[0];
                } else {
                    fb[y][x][c] = fb[y][x][c] * (1.0f - col[0]) + col[c + 1] * col[0];
                }
            }
        }
    }
}

static void
bind_art(void* user, int slot, const char* product) {
    (void)user;
    if (slot < 0 || slot >= PS2_ART_SLOTS) {
        return;
    }
    bitmap* b = &art_bmp[slot];
    if (!b->px) {
        b->w = b->h = 128;
        b->px = malloc(sizeof(uint32_t) * 128 * 128);
    }
    unsigned h = 2166136261u;
    for (const char* p = product; *p; p++) {
        h = (h ^ (unsigned char)*p) * 16777619u;
    }
    for (int y = 0; y < 128; y++) {
        for (int x = 0; x < 128; x++) {
            int r = (int)((h >> 0) & 255) * (255 - y) / 255, g = (int)((h >> 8) & 255) * (255 - y) / 255, bl = (int)((h >> 16) & 255) * (y + 60) / 255;
            int border = x < 4 || y < 4 || x > 123 || y > 123;
            b->px[y * 128 + x] = border ? 0xFFFFFFFFu : (0xFF000000u | ((uint32_t)(r & 255) << 16) | ((uint32_t)(g & 255) << 8) | (uint32_t)(bl & 255));
        }
    }
}

int
main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s PS2.PAK out.ppm [main|devices|games|card|settings] [seconds] [selection] [popup]\n", argv[0]);
        return 2;
    }
    FILE* f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = malloc((size_t)size);
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        return 1;
    }
    fclose(f);
    static ps2_pack pack;
    int err = ps2_pack_open(&pack, buf, (size_t)size);
    if (err != PS2_PACK_OK) {
        fprintf(stderr, "not a usable PS2.PAK (error %d)\n", err);
        return 1;
    }
    g_pack = &pack;
    decode_textures();

    const char* screen_name = argc > 3 ? argv[3] : "main";
    float seconds = argc > 4 ? (float)atof(argv[4]) : 3.0f;
    int sel = argc > 5 ? atoi(argv[5]) : 0;
    int popup = argc > 6 ? atoi(argv[6]) : 0;

    ps2_host host;
    memset(&host, 0, sizeof(host));
    host.game_count = h_count;
    host.game_cursor = h_cursor;
    host.game_get = h_get;
    host.game_nav = h_nav;
    host.game_folder = h_folder;
    host.recent_count = h_recent_count;
    host.recent_name = h_recent_name;
    host.set_count = h_set_count;
    host.set_label = h_set_label;
    host.set_group = h_set_group;
    host.set_help = h_set_help;
    host.set_value = h_set_value;
    host.set_change = h_set_change;
    host.memcard_present = h_memcard;
    host.clock_text = h_clock;
    host.version = h_version;
    ps2_sink sink = {NULL, tri, bind_art};

    static ps2_ui ui;
    if (ps2_ui_init(&ui, &pack, &host, &sink) != 0) {
        fprintf(stderr, "the pack lacks icon models\n");
        return 1;
    }
    int screen = PS2_SCREEN_MAIN;
    if (!strcmp(screen_name, "devices")) screen = PS2_SCREEN_DEVICES;
    if (!strcmp(screen_name, "games")) screen = PS2_SCREEN_GAMES;
    if (!strcmp(screen_name, "card")) screen = PS2_SCREEN_CARD;
    if (!strcmp(screen_name, "settings")) screen = PS2_SCREEN_SETTINGS;
    if (!strcmp(screen_name, "version")) screen = PS2_SCREEN_VERSION;
    if (!strcmp(screen_name, "intro")) screen = PS2_SCREEN_INTRO;
    g_cursor = screen == PS2_SCREEN_GAMES ? sel : 0;
    ui.main_sel = screen == PS2_SCREEN_MAIN ? sel : 0;
    ui.dev_sel = screen == PS2_SCREEN_DEVICES ? sel : 2;
    ui.main_pos = (float)ui.main_sel;
    ui.dev_pos = (float)ui.dev_sel;
    ps2_ui_show(&ui, screen);
    ui.card_slot = sel & 1;
    ui.game_pos = (float)g_cursor;
    ui.settings_cursor = screen == PS2_SCREEN_SETTINGS ? sel : 0;
    ui.settings_pos = (float)ui.settings_cursor;
    if (popup) {
        ui.recent_open = screen == PS2_SCREEN_GAMES;
    }
    ui.grid_target = g_cursor / PS2_GRID_COLS > 3 ? g_cursor / PS2_GRID_COLS - 3 : 0;
    ui.grid_top = (float)ui.grid_target;
    for (float t = 0; t < seconds; t += 1.0f / 60.0f) {
        ps2_ui_update(&ui, 1.0f / 60.0f);
    }

    uint32_t top, bottom;
    ps2_ui_background(&top, &bottom);
    for (int y = 0; y < H; y++) {
        float t = (float)y / (H - 1);
        for (int c = 0; c < 3; c++) {
            int sh = 16 - 8 * c;
            float a = (float)((top >> sh) & 255), b = (float)((bottom >> sh) & 255);
            for (int x = 0; x < W; x++) {
                fb[y][x][c] = (a + (b - a) * t) / 255.0f;
            }
        }
    }
    ps2_ui_draw(&ui);

    FILE* out = fopen(argv[2], "wb");
    if (!out) {
        perror(argv[2]);
        return 1;
    }
    fprintf(out, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            for (int c = 0; c < 3; c++) {
                float v = fb[y][x][c];
                fputc((int)(v < 0 ? 0 : v > 1 ? 255 : v * 255 + 0.5f), out);
            }
        }
    }
    fclose(out);
    printf("wrote %s (%u triangles)\n", argv[2], ui.draw.tris);
    return 0;
}
