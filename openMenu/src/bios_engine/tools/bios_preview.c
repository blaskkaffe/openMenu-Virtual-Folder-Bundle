/*
 * bios_preview: render the BIOS-style menu to a PPM image on the PC, using the
 * same engine code as the console build and a software rasteriser instead of
 * the PVR. For checking models, scripts and projection without hardware.
 *
 *   bios_preview dc_boot.bin out.ppm [frames=120] [selected=0] [script=-1]
 *
 * Set BIOS_PREVIEW_LOGO=LOGO.PVR to see a replacement header logo.
 *
 * With script >= 0 only that single script is run as an object (useful to look
 * at one model); otherwise the full main menu is shown.
 * Text surfaces are shown as outlines: the console draws them with the BIOS font.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bios_menu.h"
#include "bios_page.h"
#include "bios_list.h"
#include "bios_datetime.h"
#include "bios_files.h"
#include "tex_decode.h"
#include "bios_models.h"
#include "bios_case.h"

#define W 640
#define H 480

static float fb[H][W][3];

typedef struct {
    uint32_t* px;
    int w, h;
} bitmap;

typedef struct {
    bscene_texref ref;
    bitmap bmp;
    int valid;
} cached_tex;

static cached_tex cache[256];
static int cache_n;
static const bios_rom* g_rom;

static bitmap logo_bmp; /* optional replacement of the header logo, see biologo.py */

/* BIOS_PREVIEW_ART_<n> (disc label of row n) and BIOS_PREVIEW_ART_CASE (front of the case): P6 PPM files */
static int
load_ppm(const char* path, bitmap* b) {
    FILE* f = fopen(path, "rb");
    int w, h, mx;
    if (!f || fscanf(f, "P6 %d %d %d", &w, &h, &mx) != 3) {
        if (f) fclose(f);
        return 0;
    }
    fgetc(f);
    b->px = malloc(sizeof(uint32_t) * (size_t)w * (size_t)h);
    for (int i = 0; i < w * h; i++) {
        int r = fgetc(f), g = fgetc(f), bl = fgetc(f);
        b->px[i] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
    }
    fclose(f);
    b->w = w;
    b->h = h;
    return 1;
}

static const bitmap*
lookup(bscene_texref ref) {
    if (logo_bmp.px && ref.kind == BSCENE_TEX_TEXLIST && ref.a == BMENU_HEADER_MODEL && ref.b == BMENU_LOGO_SLOT) {
        return &logo_bmp;
    }
    for (int i = 0; i < cache_n; i++) {
        if (cache[i].ref.kind == ref.kind && cache[i].ref.a == ref.a && cache[i].ref.b == ref.b) {
            return cache[i].valid ? &cache[i].bmp : NULL;
        }
    }
    if (cache_n >= 256) {
        return NULL;
    }
    cached_tex* c = &cache[cache_n++];
    c->ref = ref;
    {
        char name[40] = "";
        if (ref.kind == BSCENE_TEX_TEXLIST && ref.a >= 0x1000 && ref.b == 0) {
            snprintf(name, sizeof(name), "BIOS_PREVIEW_ART_%d", ref.a - 0x1000);
        } else if (ref.kind == BSCENE_TEX_TEXLIST && ref.a >= BMODEL_BASE && ref.a < BMODEL_END && ref.b == BMODEL_TEX_FRONT) {
            snprintf(name, sizeof(name), "BIOS_PREVIEW_ART_CASE");
        }
        if (name[0] && getenv(name) && load_ppm(getenv(name), &c->bmp)) {
            c->valid = 1;
            return &c->bmp;
        }
    }
    if (ref.kind == BSCENE_TEX_TEXLIST && ref.a >= BMODEL_BASE && ref.a < BMODEL_END) {
        /* built-in models: a test chart for the front picture, plain white for the back */
        c->bmp.w = c->bmp.h = 64;
        c->bmp.px = malloc(sizeof(uint32_t) * 64 * 64);
        for (int i = 0; i < 64 * 64; i++) {
            int x = i % 64, y = i / 64;
            uint32_t col = 0xFFFFFFFFu;
            if (ref.b == BMODEL_TEX_FRONT) {
                col = ((x / 8 + y / 8) & 1) ? 0xFFF0A020u : 0xFF20A0F0u;
                if (x < 3 || y < 3 || x > 60 || y > 60) col = 0xFF202020u;
                if (y < 12 && x < 24) col = 0xFFFF2020u; /* top-left marker shows the orientation */
            }
            c->bmp.px[i] = col;
        }
        c->valid = c->bmp.px != NULL;
        return c->valid ? &c->bmp : NULL;
    }
    bios_texture t;
    int ok = (ref.kind == BSCENE_TEX_GBIX) ? bios_texture_find(g_rom, (uint32_t)ref.a, &t) == 0
                                            : bios_texlist_texture(g_rom, ref.a >= 0x1000 ? 61 : ref.a, ref.b, &t) == 0;
    if (ok) {
        c->bmp.w = t.width;
        c->bmp.h = t.height;
        c->bmp.px = malloc(sizeof(uint32_t) * t.width * t.height);
        c->valid = c->bmp.px && bios_texture_decode(&t, c->bmp.px) == 0;
    }
    return c->valid ? &c->bmp : NULL;
}

static void
tri(void* user, const bscene_vtx v[3], bscene_texref ref) {
    (void)user;
    const bitmap* tex = ref.kind != BSCENE_TEX_NONE ? lookup(ref) : NULL;
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
            if (w0 < 0 || w1 < 0 || w2 < 0) {
                continue;
            }
            float iw = w0 * v[0].invw + w1 * v[1].invw + w2 * v[2].invw;
            float col[4];
            for (int c = 0; c < 4; c++) {
                int sh = 24 - 8 * c;
                col[c] = (w0 * (float)((v[0].argb >> sh) & 255) * v[0].invw + w1 * (float)((v[1].argb >> sh) & 255) * v[1].invw
                          + w2 * (float)((v[2].argb >> sh) & 255) * v[2].invw) / iw / 255.0f;
            }
            if (tex) {
                float u = (w0 * v[0].u * v[0].invw + w1 * v[1].u * v[1].invw + w2 * v[2].u * v[2].invw) / iw;
                float t = (w0 * v[0].v * v[0].invw + w1 * v[1].v * v[1].invw + w2 * v[2].v * v[2].invw) / iw;
                int tx = (int)floorf(u * tex->w) % tex->w, ty = (int)floorf(t * tex->h) % tex->h;
                if (tx < 0) tx += tex->w;
                if (ty < 0) ty += tex->h;
                uint32_t p = tex->px[ty * tex->w + tx];
                col[0] *= (float)(p >> 24) / 255.0f;
                col[1] *= (float)((p >> 16) & 255) / 255.0f;
                col[2] *= (float)((p >> 8) & 255) / 255.0f;
                col[3] *= (float)(p & 255) / 255.0f;
            }
            for (int c = 0; c < 3; c++) {
                fb[y][x][c] = fb[y][x][c] * (1.0f - col[0]) + col[c + 1] * col[0];
            }
        }
    }
}

static void
text(void* user, const bvm_obj* o, float x, float y, float invw) {
    (void)user; (void)invw;
    int x0 = (int)(x - o->text_w / 2.0f), y0 = (int)(y - o->text_h / 2.0f), x1 = x0 + o->text_w, y1 = y0 + o->text_h; /* anchor = centre */
    if (getenv("BIOS_PREVIEW_NOBOX")) { /* print the text areas instead of outlining them (id x y w h) */
        fprintf(stderr, "TEXT %x %d %d %d %d\n", o->id, x0, y0, o->text_w, o->text_h);
        return;
    }
    for (int i = x0; i < x1; i++) {
        for (int k = 0; k < 2; k++) {
            int yy = k ? y1 - 1 : y0;
            if (i >= 0 && i < W && yy >= 0 && yy < H) fb[yy][i][0] = fb[yy][i][1] = fb[yy][i][2] = 1.0f;
        }
    }
    for (int j = y0; j < y1; j++) {
        for (int k = 0; k < 2; k++) {
            int xx = k ? x1 - 1 : x0;
            if (j >= 0 && j < H && xx >= 0 && xx < W) fb[j][xx][0] = fb[j][xx][1] = fb[j][xx][2] = 1.0f;
        }
    }
}

static void
demo_row(void* user, int index, bpage_row* out) {
    (void)user;
    out->icon = index < 4 ? BPAGE_ICON_BIOS(index) : BPAGE_ICON_DIGIT(index - 4);
}

int
main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s dc_boot.bin out.ppm [frames] [selected] [script]\n", argv[0]);
        return 2;
    }
    int frames = argc > 3 ? atoi(argv[3]) : 120, selected = argc > 4 ? atoi(argv[4]) : 0, script = argc > 5 ? atoi(argv[5]) : -1;

    FILE* f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 1;
    }
    static uint8_t rom_data[BIOS_ROM_SIZE];
    size_t got = fread(rom_data, 1, sizeof(rom_data), f);
    fclose(f);
    static bios_rom rom;
    int err = bios_rom_init(&rom, rom_data, got);
    if (err != BIOS_ROM_OK) {
        fprintf(stderr, "not a supported boot ROM (error %d)\n", err);
        return 1;
    }
    g_rom = &rom;

    const char* logo_path = getenv("BIOS_PREVIEW_LOGO");
    if (logo_path && *logo_path) {
        FILE* lf = fopen(logo_path, "rb");
        static uint8_t logo_file[256 * 1024];
        size_t n = lf ? fread(logo_file, 1, sizeof(logo_file), lf) : 0;
        if (lf) {
            fclose(lf);
        }
        bios_texture lt;
        if (bios_texture_parse(logo_file, n, &lt) == 0) {
            logo_bmp.w = lt.width;
            logo_bmp.h = lt.height;
            logo_bmp.px = malloc(sizeof(uint32_t) * lt.width * lt.height);
            if (!logo_bmp.px || bios_texture_decode(&lt, logo_bmp.px) != 0) {
                logo_bmp.px = NULL;
            }
        }
        if (!logo_bmp.px) {
            fprintf(stderr, "could not use %s as logo\n", logo_path);
        }
    }

    static bmenu menu;
    static bpage page;
    static blist list;
    static bdt dt;
    bmenu_init(&menu, &rom, NULL);
    if (script == -2) { /* settings page demo: `selected` = cursor row, 10 rows */
        bpage_open(&page, &menu, 10);
        for (int i = 0; i < selected; i++) {
            bpage_move(&page, 1);
        }
    } else if (script == -3 || script == -4) { /* game list demo: 7 rows; -4 = launch animation at `frames` */
        blist_open(&list, &menu, getenv("BLIST_SLOTS") ? atoi(getenv("BLIST_SLOTS")) : 7, 30);
        blist_goto(&list, selected);
    } else if (script == -6) { /* date and time editor, `selected` = field */
        bdt_open(&dt, &menu, BDT_ORDER_MDY, 2026, 10, 9, 14, 30);
        dt.cursor = selected;
    } else if (script == -7) { /* model 35 (the green triangle) as the mouse pointer, `selected` = rotation in degrees */
        bvm_obj* o = bvm_create(&menu.vm, 0x3d, 0x500, 0x2000);
        bmenu_update(&menu);
        o->flags &= ~(uint32_t)BVM_F_ATTACHED;
        o->pos_tw[0].cur = -8.0f;
        o->pos_tw[1].cur = 6.0f;
        o->pos_tw[2].cur = -353.5f;
        o->scale_tw[0].cur = o->scale_tw[1].cur = o->scale_tw[2].cur = 1.0f;
        o->rot_tw[2].cur = (int32_t)((float)selected * 65536.0f / 360.0f);
        o->rot_tw[2].step = 0;
        bmenu_update(&menu);
    } else if (script == -10) { /* built-in models: `selected` = model index from BMODEL_BASE, BIOS_PREVIEW_ROT="x,y,z" degrees */
        bvm_obj* o = bvm_create(&menu.vm, 0x3d, 0x500, 0x2000);
        bmenu_update(&menu);
        o->flags &= ~(uint32_t)(BVM_F_ATTACHED | BVM_F_MOTION);
        o->model = BMODEL_BASE + selected;
        o->texlist = o->model;
        o->pos_tw[0].cur = 0.0f;
        o->pos_tw[1].cur = -3.0f;
        o->pos_tw[2].cur = getenv("BIOS_PREVIEW_Z") ? -(float)atof(getenv("BIOS_PREVIEW_Z")) : -140.0f;
        o->scale_tw[0].cur = o->scale_tw[1].cur = o->scale_tw[2].cur = 1.0f;
        float rot[3] = {0, 0, 0};
        if (getenv("BIOS_PREVIEW_ROT")) {
            sscanf(getenv("BIOS_PREVIEW_ROT"), "%f,%f,%f", &rot[0], &rot[1], &rot[2]);
        }
        for (int k = 0; k < 3; k++) {
            o->rot_tw[k].cur = (int32_t)(rot[k] * 65536.0f / 360.0f);
            o->rot_tw[k].step = 0;
        }
        bmenu_update(&menu);
    } else if (script == -11) { /* case flying in: `selected` = frames since the selection moved down */
        bmenu_update(&menu);
    } else if (script == -9) { /* memory card grid of the File screen: `selected` = cursor; cards in A1, B1, B2 */
        static bfiles bf;
        bfiles_open(&bf, &menu, selected);
        bf.present[0] = bf.present[2] = bf.present[3] = 1;
        for (int i = 0; i < 60; i++) {
            bmenu_update(&menu);
            bfiles_sync(&bf);
        }
        menu.vm.error = 0;
    } else if (script == -8) { /* window panel with the accent of screen `selected` (0 orange 1 green 2 blue 3 magenta 4 grey) */
    } else if (script == -5) { /* the GD-ROM disc model seen from behind, large */
        bvm_obj* o = bvm_create(&menu.vm, 0x4e, 0x500, 0x2000);
        bmenu_update(&menu);
        o->flags &= ~(uint32_t)(BVM_F_ATTACHED | BVM_F_MOTION);
        o->model = 61;
        o->texlist = 61;
        o->pos_tw[0].cur = o->pos_tw[1].cur = 0;
        o->pos_tw[2].cur = -378.0f;
        o->scale_tw[0].cur = o->scale_tw[1].cur = o->scale_tw[2].cur = 0.8f;
        o->rot_tw[1].cur = selected; /* angle units, 0x8000 = 180 degrees */
        bmenu_update(&menu);
    } else if (script >= 0) {
        bvm_obj* o = bvm_create(&menu.vm, script, 0x400, 0x2000);
        if (!o) {
            fprintf(stderr, "script %d is unused\n", script);
            return 1;
        }
        o->pos_tw[2].cur = -340.0f; /* scripts that do not place themselves */
    } else {
        bmenu_show_main(&menu, selected);
    }
    for (int i = 0; i < (script == -5 || script == -7 || script == -10 || script == -11 || script == -8 || script == -9 ? 0 : frames); i++) {
        bmenu_update(&menu);
        if (script == -2) {
            bpage_sync(&page, demo_row, NULL);
        }
        if (script == -6) {
            bdt_sync(&dt);
        }
        if (script == -3 || script == -4) {
            if (script == -4 && i == 20) {
                blist_launch_start(&list);
            }
            blist_launch_step(&list);
            list.multi[0] = list.multi[2] = list.multi[4] = 1; /* demo: some rows are multi-disc sets */
            blist_sync(&list);
        }
    }
    if (menu.vm.error) {
        fprintf(stderr, "script error %d at %#x\n", menu.vm.error, menu.vm.error_pc);
    }

    uint32_t top, bottom;
    dcbg_gradient(&menu.bg, &top, &bottom);
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
    bscene_sink sink = {NULL, tri, text};
    if (script == -9) {
        bscene_draw_background(&menu.bg, &sink);
        bmenu_draw_objects(&menu, &sink);
    } else if (script == -8) {
        static const uint32_t acc[5] = {0xFFE07000u, 0xFF00E070u, 0xFF0070E0u, 0xFFE00070u, 0xFFE0E0E0u};
        bscene_draw_background(&menu.bg, &sink);
        float pr[4] = {82.0f, 97.0f, 476.0f, 286.0f}; /* BIOS_PREVIEW_PANEL="x,y,w,h" */
        if (getenv("BIOS_PREVIEW_PANEL")) {
            sscanf(getenv("BIOS_PREVIEW_PANEL"), "%f,%f,%f,%f", &pr[0], &pr[1], &pr[2], &pr[3]);
        }
        bscene_draw_panel(&menu.scene, pr[0], pr[1], pr[2], pr[3], acc[selected % 5], &sink);
    } else if (script == -6) {
        bscene_draw_background(&menu.bg, &sink);
        bscene_draw_panel(&menu.scene, BDT_PANEL_X, BDT_PANEL_Y, BDT_PANEL_W, BDT_PANEL_H, 0xFFE00070u, &sink);
        bdt_draw(&dt, &sink);
    } else if (script == -11) {
        static bcase bc;
        bscene_draw_background(&menu.bg, &sink);
        bcase_init(&bc);
        bcase_show(&bc, BMODEL_CASE_PAL, "A", 1);
        for (int i = 0; i < 200; i++) {
            bcase_step(&bc);
        }
        bcase_show(&bc, BMODEL_CASE_WHITE, "B", 1);
        for (int i = 0; i < selected; i++) {
            bcase_step(&bc);
        }
        bscene_draw_panel(&menu.scene, 432.0f, 58.0f, 194.0f, 326.0f, 0xFFE07000u, &sink);
        bcase_draw(&bc, &menu.scene, 529.0f, 136.0f, 150.0f, NULL, NULL, &sink);
    } else if (script == -7 || script == -10) {
        bscene_draw_background(&menu.bg, &sink);
        bmenu_draw_objects(&menu, &sink);
    } else if (script == -3 || script == -4) {
        bscene_draw_background(&menu.bg, &sink);
        blist_draw(&list, &sink);
        if (getenv("BIOS_PREVIEW_CASE")) { /* the right panel of the game browser with the CD case at rest */
            static bcase bc;
            bcase_init(&bc);
            bcase_show(&bc, atoi(getenv("BIOS_PREVIEW_CASE")) ? BMODEL_CASE_PAL : BMODEL_CASE_WHITE, "A", 1);
            for (int i = 0; i < 200; i++) {
                bcase_step(&bc);
            }
            { /* flat rounded panel in the colour of the row bars (as the app's gfx_rrect) */
                float pt, pb;
                blist_rows_extent_px(&list, &pt, &pb);
                float x = 432.0f, y = pt, w = 194.0f, h = pb - pt, r = 9.0f;
                float cxs[4] = {x + w - r, x + r, x + r, x + w - r}, cys[4] = {y + r, y + r, y + h - r, y + h - r};
                float dxs[4] = {1, -1, -1, 1}, dys[4] = {-1, -1, 1, 1};
                float rect[3][4] = {{x + r, y, w - 2 * r, r}, {x, y + r, w, h - 2 * r}, {x + r, y + h - r, w - 2 * r, r}};
                for (int i = 0; i < 3; i++) {
                    bscene_vtx q[4];
                    float xs[4] = {rect[i][0], rect[i][0] + rect[i][2], rect[i][0], rect[i][0] + rect[i][2]};
                    float ys[4] = {rect[i][1], rect[i][1], rect[i][1] + rect[i][3], rect[i][1] + rect[i][3]};
                    for (int k = 0; k < 4; k++) {
                        q[k] = (bscene_vtx){xs[k], ys[k], 0.003f, 0, 0, 0xB25A5AA0u};
                    }
                    bscene_vtx a[3] = {q[0], q[1], q[2]}, b2[3] = {q[1], q[3], q[2]};
                    sink.triangle(sink.user, a, (bscene_texref){BSCENE_TEX_NONE, 0, 0});
                    sink.triangle(sink.user, b2, (bscene_texref){BSCENE_TEX_NONE, 0, 0});
                }
                for (int c = 0; c < 4; c++) {
                    for (int i = 0; i < 6; i++) {
                        float a0 = 1.5707963f * i / 6, a1 = 1.5707963f * (i + 1) / 6;
                        bscene_vtx v[3] = {{cxs[c], cys[c], 0.003f, 0, 0, 0xB25A5AA0u},
                                           {cxs[c] + dxs[c] * r * cosf(a0), cys[c] + dys[c] * r * sinf(a0), 0.003f, 0, 0, 0xB25A5AA0u},
                                           {cxs[c] + dxs[c] * r * cosf(a1), cys[c] + dys[c] * r * sinf(a1), 0.003f, 0, 0, 0xB25A5AA0u}};
                        sink.triangle(sink.user, v, (bscene_texref){BSCENE_TEX_NONE, 0, 0});
                    }
                }
            }
            bcase_draw(&bc, &menu.scene, 529.0f, 8.0f + 75.0f + ({float t_, b_; blist_rows_extent_px(&list, &t_, &b_); t_;}), 150.0f, NULL, NULL, &sink);
        }
    } else if (script == -2) {
        bscene_draw_background(&menu.bg, &sink);
        bpage_draw(&page, &sink);
    } else {
        bmenu_draw(&menu, &sink);
    }

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
    printf("wrote %s (%d frames)\n", argv[2], frames);
    return 0;
}
